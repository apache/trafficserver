#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Test conf -> hrw4u conversion matches the autest .hrw4u files."""
from __future__ import annotations

from pathlib import Path

import pytest
from antlr4 import CommonTokenStream, InputStream

from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser
from u4wrh.hrw_visitor import HRWInverseVisitor

AUTEST_RULES_DIR = Path(
    __file__).resolve().parent.parent.parent.parent / "tests" / "gold_tests" / "pluginTest" / "header_rewrite" / "rules"


def _collect_autest_pairs() -> list[pytest.param]:
    """Collect .conf/.hrw4u pairs from the autest rules directory."""
    if not AUTEST_RULES_DIR.is_dir():
        return []

    pairs = []
    for conf in sorted(AUTEST_RULES_DIR.glob("*.conf")):
        hrw4u = conf.with_suffix(".hrw4u")
        if hrw4u.exists():
            pairs.append(pytest.param(conf, hrw4u, id=conf.stem))

    return pairs


@pytest.mark.parametrize("conf_file,hrw4u_file", _collect_autest_pairs())
def test_conf_to_hrw4u(conf_file: Path, hrw4u_file: Path) -> None:
    """Test that conf -> hrw4u output matches the .hrw4u file."""
    text = conf_file.read_text()
    lexer = u4wrhLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = u4wrhParser(stream)
    tree = parser.program()

    visitor = HRWInverseVisitor(filename=str(conf_file), merge_sections=False)
    result = visitor.visit(tree)

    assert result is not None, f"u4wrh produced no output for {conf_file.name}"
    assert len(result) > 0, f"u4wrh produced empty output for {conf_file.name}"

    expected = hrw4u_file.read_text().strip()
    actual = '\n'.join(result).strip()

    assert actual == expected, (f"u4wrh output for {conf_file.name} does not match {hrw4u_file.name}")
