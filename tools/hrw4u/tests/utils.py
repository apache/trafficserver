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

import re
import subprocess
import sys
import tempfile
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

# Try to import structured error type, fall back to generic Exception if not available
try:
    from src.errors import Hrw4uSyntaxError
except ImportError:
    # Fallback: define a minimal interface for structured error detection
    class Hrw4uSyntaxError(Exception):

        def __init__(self, filename: str, line: int, column: int, message: str, source_line: str = ""):
            super().__init__(message)
            self.filename = filename
            self.line = line
            self.column = column
            self.source_line = source_line


__all__: Final[list[str]] = [
    "collect_output_test_files",
    "collect_ast_test_files",
    "collect_failing_inputs",
    "run_output_test",
    "run_ast_test",
    "run_failing_test",
    "run_reverse_test",
    "run_bulk_test",
]


def parse_input_text(text: str) -> tuple[hrw4uParser, hrw4uParser.ProgramContext]:
    """Parse hrw4u input text and return parser and AST."""
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
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
            if direction != "both" and direction != test_direction:
                continue

        yield pytest.param(input_file, output_file, id=test_id)


def collect_ast_test_files(group: str) -> Iterator[pytest.param]:
    """
    Collect test files for AST validation tests.

    AST tests always run in hrw4u direction only since they validate
    the parse tree structure of the input format.
    """
    base_dir = Path("tests/data") / group

    for input_file in base_dir.glob("*.input.txt"):
        if ".fail." in input_file.name:
            continue

        base = input_file.with_suffix('')
        ast_file = base.with_suffix('.ast.txt')
        test_id = base.name

        if ast_file.exists():
            yield pytest.param(input_file, ast_file, id=test_id)


def collect_failing_inputs(group: str) -> Iterator[pytest.param]:
    """
    Collect test files for failure validation tests.
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
    """Run failure validation test ensuring input produces expected error with structured validation."""
    text = input_file.read_text()
    parser, tree = parse_input_text(text)
    visitor = HRW4UVisitor(filename=str(input_file))

    error_file = input_file.with_name(input_file.name.replace(".input.txt", ".error.txt"))

    if not error_file.exists():
        raise RuntimeError(f"Missing expected error file: {error_file}")

    expected_error_content = error_file.read_text().strip()

    with pytest.raises(Exception) as exc_info:
        visitor.visit(tree)

    actual_exception = exc_info.value
    actual_error_str = str(actual_exception).strip()

    # Parse expected error for structured validation
    expected_fields = _parse_error_file(expected_error_content)

    if expected_fields and isinstance(actual_exception, Hrw4uSyntaxError):
        # Assert structured fields when available
        _assert_structured_error_fields(actual_exception, expected_fields, input_file)
    else:
        # Fallback to substring matching for legacy files or non-structured exceptions
        assert expected_error_content in actual_error_str, (
            f"Error mismatch for {input_file}\n"
            f"Expected error (partial match):\n{expected_error_content}\n\n"
            f"Actual error:\n{actual_error_str}")


def _parse_error_file(error_content: str) -> dict[str, str | int] | None:
    """
    Parse structured error file content to extract filename, line, column, and message.

    Expected format: filename:line:column: error: message
    Returns None if parsing fails (fallback to substring matching).
    """
    lines = error_content.strip().split('\n')
    if not lines:
        return None

    first_line = lines[0].strip()

    # Regex to parse: filename:line:column: error: message
    error_pattern = re.compile(r'^(.+):(\d+):(\d+):\s*error:\s*(.+)$')
    match = error_pattern.match(first_line)

    if not match:
        return None

    filename, line_str, col_str, message = match.groups()

    try:
        return {'filename': filename.strip(), 'line': int(line_str), 'column': int(col_str), 'message': message.strip()}
    except ValueError:
        return None


def _assert_structured_error_fields(
        actual_exception: Hrw4uSyntaxError, expected_fields: dict[str, str | int], input_file: Path) -> None:
    """Assert that structured exception fields match expected values."""

    # Assert filename (normalize paths for comparison)
    expected_filename = str(Path(expected_fields['filename']).resolve())
    actual_filename = str(Path(actual_exception.filename).resolve())
    assert actual_filename == expected_filename, (
        f"Filename mismatch for {input_file}\n"
        f"Expected: {expected_filename}\n"
        f"Actual: {actual_filename}")

    # Assert line number
    assert actual_exception.line == expected_fields['line'], (
        f"Line number mismatch for {input_file}\n"
        f"Expected: {expected_fields['line']}\n"
        f"Actual: {actual_exception.line}")

    # Assert column number
    assert actual_exception.column == expected_fields['column'], (
        f"Column number mismatch for {input_file}\n"
        f"Expected: {expected_fields['column']}\n"
        f"Actual: {actual_exception.column}")

    # Assert error message (allow partial match for flexibility)
    expected_message = expected_fields['message']
    actual_full_error = str(actual_exception)
    assert expected_message in actual_full_error, (
        f"Error message mismatch for {input_file}\n"
        f"Expected message (partial): '{expected_message}'\n"
        f"Actual full error:\n{actual_full_error}")


def run_reverse_test(input_file: Path, output_file: Path) -> None:
    """Run u4wrh on output.txt and compare with input.txt (round-trip test)."""
    output_text = output_file.read_text()
    lexer = u4wrhLexer(InputStream(output_text))
    stream = CommonTokenStream(lexer)
    parser = u4wrhParser(stream)
    tree = parser.program()
    visitor = HRWInverseVisitor(filename=str(output_file))
    actual_hrw4u = "\n".join(visitor.visit(tree)).strip()
    expected_hrw4u = input_file.read_text().strip()
    assert actual_hrw4u == expected_hrw4u, f"Reverse conversion mismatch for {output_file}"


def create_output_test(group: str):
    """Create a parametrized output test function for a specific group."""
    import pytest

    @pytest.mark.parametrize("input_file,output_file", collect_output_test_files(group, "hrw4u"))
    def test_output_matches(input_file: Path, output_file: Path) -> None:
        f"""Test that hrw4u output matches expected output for {group} test cases."""
        run_output_test(input_file, output_file)

    return test_output_matches


def create_ast_test(group: str):
    """Create a parametrized AST test function for a specific group."""
    import pytest

    @pytest.mark.ast
    @pytest.mark.parametrize("input_file,ast_file", collect_ast_test_files(group))
    def test_ast_matches(input_file: Path, ast_file: Path) -> None:
        f"""Test that AST structure matches expected AST for {group} test cases."""
        run_ast_test(input_file, ast_file)

    return test_ast_matches


def create_invalid_test(group: str):
    """Create a parametrized invalid input test function for a specific group."""
    import pytest

    @pytest.mark.invalid
    @pytest.mark.parametrize("input_file", collect_failing_inputs(group))
    def test_invalid_inputs_fail(input_file: Path) -> None:
        f"""Test that invalid {group} inputs produce expected errors."""
        run_failing_test(input_file)

    return test_invalid_inputs_fail


def create_reverse_test(group: str):
    """Create a parametrized reverse test function for a specific group."""
    import pytest

    @pytest.mark.reverse
    @pytest.mark.parametrize("input_file,output_file", collect_output_test_files(group, "u4wrh"))
    def test_reverse_conversion(input_file: Path, output_file: Path) -> None:
        f"""Test that u4wrh reverse conversion produces original hrw4u for {group} test cases."""
        run_reverse_test(input_file, output_file)

    return test_reverse_conversion


def run_bulk_test(group: str) -> None:
    """
    Run bulk compilation test for a specific test group.

    Collects all .input.txt files in the group, runs hrw4u with bulk
    input:output pairs, and compares each output with expected .output.txt.
    """
    base_dir = Path("tests/data") / group
    exceptions = _read_exceptions(base_dir)

    input_files = []
    expected_outputs = []
    file_pairs = []

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)

        for input_file in sorted(base_dir.glob("*.input.txt")):
            if ".fail." in input_file.name:
                continue

            base = input_file.with_suffix('')
            expected_output_file = base.with_suffix('.output.txt')
            test_id = base.name

            if test_id in exceptions:
                test_direction = exceptions[test_id]
                if test_direction != "hrw4u":
                    continue

            if not expected_output_file.exists():
                continue

            input_files.append(input_file)
            expected_outputs.append(expected_output_file)

            actual_output_file = tmp_path / f"{input_file.stem}.output.txt"
            file_pairs.append(f"{input_file.resolve()}:{actual_output_file.resolve()}")

        if not file_pairs:
            pytest.skip(f"No valid test files found for bulk test in {group}")
            return

        hrw4u_script = Path("scripts/hrw4u").resolve()
        cmd = [sys.executable, str(hrw4u_script)] + file_pairs

        result = subprocess.run(cmd, capture_output=True, text=True, cwd=Path.cwd())

        if result.returncode != 0:
            pytest.fail(f"hrw4u bulk compilation failed:\nstdout: {result.stdout}\nstderr: {result.stderr}")

        for input_file, expected_output_file in zip(input_files, expected_outputs):
            actual_output_file = tmp_path / f"{input_file.stem}.output.txt"

            if not actual_output_file.exists():
                pytest.fail(f"Output file not created for {input_file.name}: {actual_output_file}")

            actual_output = actual_output_file.read_text().strip()
            expected_output = expected_output_file.read_text().strip()

            assert actual_output == expected_output, (
                f"Bulk output mismatch for {input_file.name}\n"
                f"Expected:\n{expected_output}\n\n"
                f"Actual:\n{actual_output}")
