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
"""Test hrw4u -> conf conversion matches the autest .conf files."""
from __future__ import annotations

from pathlib import Path

import pytest
from antlr4 import CommonTokenStream, InputStream

from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from hrw4u.errors import ErrorCollector

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
def test_hrw4u_to_conf(conf_file: Path, hrw4u_file: Path) -> None:
    """Test that hrw4u -> conf output matches the .conf file."""
    text = hrw4u_file.read_text()
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()

    ec = ErrorCollector(max_errors=10)
    visitor = HRW4UVisitor(filename=str(hrw4u_file), error_collector=ec)
    result = visitor.visit(tree)

    assert not ec.has_errors(), f"hrw4u errors in {hrw4u_file.name}:\n{ec.get_error_summary()}"
    assert result is not None, f"hrw4u produced no output for {hrw4u_file.name}"

    expected = conf_file.read_text().strip()
    actual = '\n'.join(result).strip()

    assert actual == expected, (f"hrw4u output for {hrw4u_file.name} does not match {conf_file.name}")
