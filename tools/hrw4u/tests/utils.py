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
from hrw4u.sandbox import SandboxConfig
from hrw4u.errors import ErrorCollector
from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser
from u4wrh.hrw_visitor import HRWInverseVisitor

from hrw4u.errors import Hrw4uSyntaxError

__all__: Final[list[str]] = [
    "collect_output_test_files",
    "collect_ast_test_files",
    "collect_failing_inputs",
    "collect_sandbox_deny_test_files",
    "collect_sandbox_allow_test_files",
    "run_output_test",
    "run_ast_test",
    "run_failing_test",
    "run_sandbox_deny_test",
    "run_sandbox_allow_test",
    "run_reverse_test",
    "run_bulk_test",
    "run_procedure_output_test",
    "run_procedure_flatten_test",
    "run_procedure_failing_test",
    "run_procedure_flatten_roundtrip_test",
    "collect_flatten_test_files",
]


def parse_input_text(text: str) -> tuple[hrw4uParser, hrw4uParser.ProgramContext]:
    lexer = hrw4uLexer(InputStream(text))
    stream = CommonTokenStream(lexer)
    parser = hrw4uParser(stream)
    tree = parser.program()
    return parser, tree


def _read_exceptions(base_dir: Path) -> dict[str, str]:
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
    base_dir = Path("tests/data") / group
    exceptions = _read_exceptions(base_dir)

    for input_file in base_dir.glob("*.input.txt"):
        if ".fail." in input_file.name:
            continue

        base = input_file.with_suffix('')
        output_file = base.with_suffix('.output.txt')
        test_id = base.name

        if test_id in exceptions:
            test_direction = exceptions[test_id]
            if direction != "both" and direction != test_direction:
                continue

        yield pytest.param(input_file, output_file, id=test_id)


def collect_ast_test_files(group: str) -> Iterator[pytest.param]:
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
    base_dir = Path("tests/data") / group
    for input_file in base_dir.glob("*.fail.input.txt"):
        test_id = input_file.stem
        yield pytest.param(input_file, id=test_id)


def _collect_sandbox_test_files(group: str, result_suffix: str) -> Iterator[pytest.param]:
    """Collect sandbox test files: (input, result, sandbox_config).

    Uses a per-test `{name}.sandbox.yaml` if present, otherwise falls back
    to a shared `sandbox.yaml` in the same directory.
    """
    base_dir = Path("tests/data") / group
    shared_sandbox = base_dir / "sandbox.yaml"

    for input_file in sorted(base_dir.glob("*.input.txt")):
        base = input_file.with_suffix("").with_suffix("")
        result_file = base.with_suffix(result_suffix)

        if not result_file.exists():
            continue

        per_test_sandbox = base.with_suffix(".sandbox.yaml")
        sandbox_file = per_test_sandbox if per_test_sandbox.exists() else shared_sandbox

        if not sandbox_file.exists():
            continue

        yield pytest.param(input_file, result_file, sandbox_file, id=base.name)


def collect_sandbox_deny_test_files(group: str) -> Iterator[pytest.param]:
    """Collect sandbox denial test files: (input, error, sandbox_config)."""
    yield from _collect_sandbox_test_files(group, ".error.txt")


def collect_sandbox_allow_test_files(group: str) -> Iterator[pytest.param]:
    """Collect sandbox allow test files: (input, output, sandbox_config)."""
    yield from _collect_sandbox_test_files(group, ".output.txt")


def run_output_test(input_file: Path, output_file: Path) -> None:
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor()
    actual_output = "\n".join(visitor.visit(tree)).strip()
    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, f"Output mismatch in {input_file}"


def run_ast_test(input_file: Path, ast_file: Path) -> None:
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    actual_ast = tree.toStringTree(recog=parser).strip()
    expected_ast = ast_file.read_text().strip()
    assert actual_ast == expected_ast, f"AST mismatch in {input_file}"


def run_failing_test(input_file: Path) -> None:
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

    expected_fields = _parse_error_file(expected_error_content)

    if expected_fields and isinstance(actual_exception, Hrw4uSyntaxError):
        _assert_structured_error_fields(actual_exception, expected_fields, input_file)
    else:
        assert expected_error_content in actual_error_str, (
            f"Error mismatch for {input_file}\n"
            f"Expected error (partial match):\n{expected_error_content}\n\n"
            f"Actual error:\n{actual_error_str}")


def _parse_error_file(error_content: str) -> dict[str, str | int] | None:
    lines = error_content.strip().split('\n')
    if not lines:
        return None

    first_line = lines[0].strip()
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
    expected_filename = str(Path(expected_fields['filename']).resolve())
    actual_filename = str(Path(actual_exception.filename).resolve())
    assert actual_filename == expected_filename, (
        f"Filename mismatch for {input_file}\n"
        f"Expected: {expected_filename}\n"
        f"Actual: {actual_filename}")

    assert actual_exception.line == expected_fields['line'], (
        f"Line number mismatch for {input_file}\n"
        f"Expected: {expected_fields['line']}\n"
        f"Actual: {actual_exception.line}")

    assert actual_exception.column == expected_fields['column'], (
        f"Column number mismatch for {input_file}\n"
        f"Expected: {expected_fields['column']}\n"
        f"Actual: {actual_exception.column}")

    expected_message = expected_fields['message']
    actual_full_error = str(actual_exception)
    assert expected_message in actual_full_error, (
        f"Error message mismatch for {input_file}\n"
        f"Expected message (partial): '{expected_message}'\n"
        f"Actual full error:\n{actual_full_error}")


def run_sandbox_deny_test(input_file: Path, error_file: Path, sandbox_file: Path) -> None:
    """Run a sandbox denial test, verifying that denied features produce expected errors."""
    text = input_file.read_text()
    parser, tree = parse_input_text(text)

    sandbox = SandboxConfig.load(sandbox_file)
    error_collector = ErrorCollector()
    visitor = HRW4UVisitor(filename=str(input_file), error_collector=error_collector, sandbox=sandbox)
    visitor.visit(tree)

    assert error_collector.has_errors(), f"Expected sandbox errors but none were raised for {input_file}"

    actual_summary = error_collector.get_error_summary()
    expected_content = error_file.read_text().strip()

    for line in expected_content.splitlines():
        line = line.strip()
        if line:
            assert line in actual_summary, (
                f"Expected phrase not found in error summary for {input_file}:\n"
                f"  Missing: {line!r}\n"
                f"Actual summary:\n{actual_summary}")


def run_sandbox_allow_test(input_file: Path, output_file: Path, sandbox_file: Path) -> None:
    """Run a sandbox allow test, verifying that non-denied features compile normally."""
    text = input_file.read_text()
    parser, tree = parse_input_text(text)

    sandbox = SandboxConfig.load(sandbox_file)
    error_collector = ErrorCollector()
    visitor = HRW4UVisitor(filename=str(input_file), error_collector=error_collector, sandbox=sandbox)
    actual_output = "\n".join(visitor.visit(tree) or []).strip()

    assert not error_collector.has_errors(), (
        f"Expected no errors but sandbox denied something in {input_file}:\n"
        f"{error_collector.get_error_summary()}")

    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, f"Output mismatch in {input_file}"


def run_reverse_test(input_file: Path, output_file: Path) -> None:
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
    import pytest

    @pytest.mark.parametrize("input_file,output_file", collect_output_test_files(group, "hrw4u"))
    def test_output_matches(input_file: Path, output_file: Path) -> None:
        run_output_test(input_file, output_file)

    return test_output_matches


def create_ast_test(group: str):
    import pytest

    @pytest.mark.ast
    @pytest.mark.parametrize("input_file,ast_file", collect_ast_test_files(group))
    def test_ast_matches(input_file: Path, ast_file: Path) -> None:
        run_ast_test(input_file, ast_file)

    return test_ast_matches


def create_invalid_test(group: str):
    import pytest

    @pytest.mark.invalid
    @pytest.mark.parametrize("input_file", collect_failing_inputs(group))
    def test_invalid_inputs_fail(input_file: Path) -> None:
        run_failing_test(input_file)

    return test_invalid_inputs_fail


def create_reverse_test(group: str):
    import pytest

    @pytest.mark.reverse
    @pytest.mark.parametrize("input_file,output_file", collect_output_test_files(group, "u4wrh"))
    def test_reverse_conversion(input_file: Path, output_file: Path) -> None:
        run_reverse_test(input_file, output_file)

    return test_reverse_conversion


def run_bulk_test(group: str) -> None:
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


def _procs_dir(input_file: Path) -> Path:
    return input_file.parent / 'procs'


def collect_flatten_test_files(group: str) -> Iterator[pytest.param]:
    base_dir = Path("tests/data") / group

    for input_file in base_dir.glob("*.input.txt"):
        if ".fail." in input_file.name:
            continue

        base = input_file.with_suffix('')
        flatten_file = base.with_suffix('.flatten.txt')
        test_id = base.name

        if flatten_file.exists():
            yield pytest.param(input_file, flatten_file, id=test_id)


def run_procedure_output_test(input_file: Path, output_file: Path) -> None:
    procs_dir = _procs_dir(input_file)
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor(filename=str(input_file), proc_search_paths=[procs_dir])
    actual_output = "\n".join(visitor.visit(tree)).strip()
    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, f"Output mismatch in {input_file}"


def run_procedure_flatten_test(input_file: Path, flatten_file: Path) -> None:
    procs_dir = _procs_dir(input_file)
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor(filename=str(input_file), proc_search_paths=[procs_dir])
    actual_output = "\n".join(visitor.flatten(tree, input_text)).strip()
    expected_output = flatten_file.read_text().strip()
    assert actual_output == expected_output, f"Flatten mismatch in {input_file}"


def run_procedure_flatten_roundtrip_test(input_file: Path, output_file: Path) -> None:
    """Verify that flattened output compiles to the same header_rewrite as the original."""
    procs_dir = _procs_dir(input_file)
    input_text = input_file.read_text()
    parser, tree = parse_input_text(input_text)
    visitor = HRW4UVisitor(filename=str(input_file), proc_search_paths=[procs_dir])
    flattened = "\n".join(visitor.flatten(tree, input_text))

    # Compile the flattened output (no procedures needed — it's self-contained)
    parser2, tree2 = parse_input_text(flattened)
    visitor2 = HRW4UVisitor(filename=str(input_file))
    actual_output = "\n".join(visitor2.visit(tree2)).strip()
    expected_output = output_file.read_text().strip()
    assert actual_output == expected_output, (
        f"Flatten roundtrip mismatch in {input_file}\n"
        f"Flattened hrw4u compiles to different output than original")


def run_procedure_failing_test(input_file: Path) -> None:
    procs_dir = _procs_dir(input_file)
    text = input_file.read_text()

    error_file = input_file.with_name(input_file.name.replace(".fail.input.txt", ".fail.error.txt"))
    if not error_file.exists():
        raise RuntimeError(f"Missing expected error file: {error_file}")

    expected_error = error_file.read_text().strip()

    with pytest.raises(Exception) as exc_info:
        parser, tree = parse_input_text(text)
        HRW4UVisitor(filename=str(input_file), proc_search_paths=[procs_dir]).visit(tree)

    assert expected_error in str(exc_info.value), (
        f"Error mismatch for {input_file}\n"
        f"Expected (substring): {expected_error!r}\n"
        f"Actual: {str(exc_info.value)!r}")
