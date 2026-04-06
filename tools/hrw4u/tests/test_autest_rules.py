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
"""Tests that hrw4u and u4wrh can process the autest rule files without errors.

Discovers .conf/.hrw4u pairs in the autest header_rewrite rules directory.
Only .conf files with a matching .hrw4u are tested; unmatched files are skipped.
"""
from __future__ import annotations

from pathlib import Path

import pytest
from antlr4 import CommonTokenStream, InputStream

from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from hrw4u.errors import ErrorCollector
from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser
from u4wrh.hrw_visitor import HRWInverseVisitor

from conftest import collect_autest_pairs


@pytest.mark.autest
@pytest.mark.parametrize("conf_file,hrw4u_file", collect_autest_pairs())
def test_hrw4u_compiles(conf_file: Path, hrw4u_file: Path) -> None:
    """Test that the .hrw4u autest rule file compiles without errors."""
    text = hrw4u_file.read_text()
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()

    error_collector = ErrorCollector(max_errors=10)
    visitor = HRW4UVisitor(filename=str(hrw4u_file), error_collector=error_collector)
    result = visitor.visit(tree)

    assert not error_collector.has_errors(), (
        f"hrw4u compilation errors in {hrw4u_file.name}:\n"
        f"{error_collector.get_error_summary()}")
    assert result is not None, f"hrw4u produced no output for {hrw4u_file.name}"


@pytest.mark.autest
@pytest.mark.parametrize("conf_file,hrw4u_file", collect_autest_pairs())
def test_u4wrh_reverse_compiles(conf_file: Path, hrw4u_file: Path) -> None:
    """Test that the .conf autest rule file reverse-compiles without errors."""
    text = conf_file.read_text()
    lexer = u4wrhLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = u4wrhParser(stream)
    tree = parser.program()

    visitor = HRWInverseVisitor(filename=str(conf_file))
    result = visitor.visit(tree)

    assert result is not None, f"u4wrh produced no output for {conf_file.name}"
    assert len(result) > 0, f"u4wrh produced empty output for {conf_file.name}"
