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

from hrw4u.common import create_parse_tree
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor


def _compile(source: str):
    tree, _, errors = create_parse_tree(source, "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
    output = HRW4UVisitor(filename="<test>", error_collector=errors).visit(tree)
    return output, errors


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
@pytest.mark.invalid
@pytest.mark.parametrize("op", ["no-op", "skip-remap", "set-debug"])
def test_bare_operator_is_a_syntax_error(op: str) -> None:
    """Operators only have a call form; `name;` is not a statement."""
    _, errors = _compile(f"REMAP {{\n    {op};\n}}\n")
    assert errors.has_errors()
    assert "no viable alternative" in str(errors.errors[0])


@pytest.mark.ops
@pytest.mark.invalid
def test_bare_operator_reports_once() -> None:
    """Visiting the error-recovered node must not pile a second diagnostic onto it."""
    _, errors = _compile("REMAP {\n    no-op;\n}\n")
    assert len(errors.errors) == 1


@pytest.mark.ops
def test_zero_arg_operator_call_form_compiles() -> None:
    output, errors = _compile("REMAP {\n    no-op();\n}\n")
    assert not errors.has_errors()
    assert "no-op" in "\n".join(output)
