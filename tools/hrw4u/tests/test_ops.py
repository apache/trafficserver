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
from __future__ import annotations

from pathlib import Path

import pytest
import utils
from hrw4u.errors import ErrorCollector
from hrw4u.visitor import HRW4UVisitor


@pytest.mark.ops
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("ops", "hrw4u"))
def test_output_matches(input_file: Path, output_file: Path) -> None:
    """Test that hrw4u output matches expected output for ops test cases."""
    utils.run_output_test(input_file, output_file)


@pytest.mark.ops
@pytest.mark.ast
@pytest.mark.parametrize("input_file,ast_file", utils.collect_ast_test_files("ops"))
def test_ast_matches(input_file: Path, ast_file: Path) -> None:
    """Test that AST structure matches expected AST for ops test cases."""
    utils.run_ast_test(input_file, ast_file)


@pytest.mark.ops
@pytest.mark.invalid
@pytest.mark.parametrize("input_file", utils.collect_failing_inputs("ops"))
def test_invalid_inputs_fail(input_file):
    utils.run_failing_test(input_file)


@pytest.mark.ops
@pytest.mark.parametrize(
    "old,new,expected", [
        ('remove_query("a,b")', 'inbound.url.query.remove', 'rm-destination QUERY "a,b"'),
        ('keep_query("a,b")', 'inbound.url.query.keep', 'rm-destination QUERY "a,b" [I]'),
    ])
def test_deprecated_query_functions_warn(old: str, new: str, expected: str) -> None:
    """Deprecated query functions still compile, with a warning naming the replacement."""
    _, tree = utils.parse_input_text(f"REMAP {{\n    {old};\n}}\n")
    error_collector = ErrorCollector()
    output = "\n".join(HRW4UVisitor(filename="test.hrw4u", error_collector=error_collector).visit(tree) or [])

    assert not error_collector.has_errors(), error_collector.get_error_summary()
    assert expected in output
    summary = error_collector.get_error_summary()
    assert "deprecated" in summary
    assert new in summary
