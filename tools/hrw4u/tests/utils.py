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
from typing import Final, Iterator

import pytest
from antlr4 import InputStream, CommonTokenStream
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser
from u4wrh.hrw_visitor import HRWInverseVisitor

__all__: Final[list[str]] = [
    "collect_output_test_files",
    "collect_ast_test_files",
    "collect_reverse_test_files",
    "collect_failing_inputs",
    "run_output_test",
    "run_ast_test",
    "run_failing_test",
    "run_reverse_test",
]


def parse_input_text(text: str) -> tuple[hrw4uParser, hrw4uParser.ProgramContext]:
    """Parse hrw4u input text and return parser and AST."""
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()
    return parser, tree


def parse_u4wrh_text(text: str) -> tuple[u4wrhParser, u4wrhParser.ProgramContext]:
    """Parse u4wrh input text and return parser and AST."""
    lexer = u4wrhLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = u4wrhParser(stream)
    tree = parser.program()
    return parser, tree


def _read_exceptions(base_dir: Path) -> dict[str, str]:
    """Read exceptions.txt file and return test -> direction mapping."""
    exceptions_file = base_dir / "exceptions.txt"
    exceptions = {}

    if exceptions_file.exists():
        content = exceptions_file.read_text().strip()
        for line in content.splitlines():
            line = line.strip()
            if line and not line.startswith('#'):  # Skip empty lines and comments
                parts = line.split(':', 1)
                if len(parts) == 2:
                    test_name = parts[0].strip()
                    direction = parts[1].strip()
                    if direction in ('hrw4u', 'u4wrh'):
                        exceptions[test_name] = direction

    return exceptions


def collect_output_test_files(group: str, direction: str = "hrw4u") -> Iterator[pytest.param]:
    """
    Collect test files for output validation tests.

    Args:
        group: Test group name (e.g., "examples", "conds")
        direction: "hrw4u" (forward only), "u4wrh" (reverse only), or "both" (default)

    Yields:
        pytest.param(input_file, output_file, id=test_id)
    """
    base_dir = Path("tests/data") / group
    exceptions = _read_exceptions(base_dir)

    for input_file in base_dir.glob("*.input.txt"):
        # Skip failure test cases here; those are handled separately
        if ".fail." in input_file.name:
            continue

        base = input_file.with_suffix('')
        output_file = base.with_suffix('.output.txt')
        test_id = base.name

        # Check if this test has direction restrictions
        if test_id in exceptions:
            test_direction = exceptions[test_id]
            # Skip if this test is restricted to a different direction
            if direction != "both" and direction != test_direction:
                continue

        yield pytest.param(input_file, output_file, id=test_id)


def collect_ast_test_files(group: str) -> Iterator[pytest.param]:
    """
    Collect test files for AST validation tests.

    AST tests always run in hrw4u direction only since they validate
    the parse tree structure of the input format.

    Args:
        group: Test group name (e.g., "examples", "conds")

    Yields:
        pytest.param(input_file, ast_file, id=test_id)
    """
    base_dir = Path("tests/data") / group

    for input_file in base_dir.glob("*.input.txt"):
        # Skip failure test cases here; those are handled separately
        if ".fail." in input_file.name:
            continue

        base = input_file.with_suffix('')
        ast_file = base.with_suffix('.ast.txt')
        test_id = base.name

        if ast_file.exists():
            yield pytest.param(input_file, ast_file, id=test_id)


def collect_reverse_test_files(group: str, direction: str = "u4wrh") -> Iterator[pytest.param]:
    """
    Collect test files for reverse conversion tests.

    Args:
        group: Test group name (e.g., "examples", "conds")
        direction: "hrw4u" (forward only), "u4wrh" (reverse only), or "both" (default)

    Yields:
        pytest.param(input_file, output_file, id=test_id)
    """
    return collect_output_test_files(group, direction)


def collect_failing_inputs(group: str) -> Iterator[pytest.param]:
    """
    Collect test files for failure validation tests.

    Args:
        group: Test group name (e.g., "examples", "conds")

    Yields:
        pytest.param(input_file, id=test_id)
    """
    base_dir = Path("tests/data") / group
    for input_file in base_dir.glob("*.fail.input.txt"):
        test_id = input_file.stem
        yield pytest.param(input_file, id=test_id)


def run_output_test(input_file: Path, output_file: Path) -> None:
    """Run output validation test comparing generated output with expected."""
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor()
    actual_output = "\n".join(visitor.visit(tree)).strip()
    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, f"Output mismatch in {input_file}"


def run_ast_test(input_file: Path, ast_file: Path) -> None:
    """Run AST validation test comparing generated AST with expected."""
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    actual_ast = tree.toStringTree(recog=parser).strip()
    expected_ast = ast_file.read_text().strip()
    assert actual_ast == expected_ast, f"AST mismatch in {input_file}"


def run_failing_test(input_file: Path) -> None:
    """Run failure validation test ensuring input produces expected error."""
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


def run_reverse_test(input_file: Path, output_file: Path) -> None:
    """Run u4wrh on output.txt and compare with input.txt (round-trip test)."""
    output_text = output_file.read_text()
    parser, tree = parse_u4wrh_text(output_text)
    visitor = HRWInverseVisitor(filename=str(output_file))
    actual_hrw4u = "\n".join(visitor.visit(tree)).strip()
    expected_hrw4u = input_file.read_text().strip()
    assert actual_hrw4u == expected_hrw4u, f"Reverse conversion mismatch for {output_file}"
