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
import pytest
from pathlib import Path
from antlr4 import InputStream, CommonTokenStream
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor

__all__ = [
    "collect_test_files",
    "collect_failing_inputs",
    "run_output_test",
    "run_ast_test",
    "run_failing_test",
]


def parse_input_text(text):
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()
    return parser, tree


def collect_test_files(group: str):
    base_dir = Path("tests/data") / group
    for input_file in base_dir.glob("*.input.txt"):
        # Skip failure test cases here; those are handled separately
        if ".fail." in input_file.name:
            continue
        base = input_file.with_suffix('')
        output_file = base.with_suffix('.output.txt')
        ast_file = base.with_suffix('.ast.txt')
        test_id = base.name
        yield pytest.param(input_file, output_file, ast_file, id=test_id)


def collect_failing_inputs(group: str):
    base_dir = Path("tests/data") / group
    for input_file in base_dir.glob("*.fail.input.txt"):
        test_id = input_file.stem
        yield pytest.param(input_file, id=test_id)


def run_output_test(input_file: Path, output_file: Path):
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor()
    actual_output = "\n".join(visitor.visit(tree)).strip()
    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, f"Output mismatch in {input_file}"


def run_ast_test(input_file: Path, ast_file: Path):
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    actual_ast = tree.toStringTree(recog=parser).strip()
    expected_ast = ast_file.read_text().strip()
    assert actual_ast == expected_ast, f"AST mismatch in {input_file}"


def run_failing_test(input_file: Path):
    text = input_file.read_text()
    parser, tree = parse_input_text(text)
    visitor = HRW4UVisitor(filename=str(input_file))

    error_file = input_file.with_name(input_file.name.replace(".input.txt", ".error.txt"))

    if not error_file.exists():
        raise RuntimeError(f"Missing expected error file: {error_file}")

    expected_error = error_file.read_text().strip()

    with pytest.raises(Exception) as exc_info:
        visitor.visit(tree)

    actual_error = str(exc_info.value).strip()

    assert expected_error in actual_error, (
        f"Error mismatch for {input_file}\n"
        f"Expected error (partial match):\n{expected_error}\n\n"
        f"Actual error:\n{actual_error}")
