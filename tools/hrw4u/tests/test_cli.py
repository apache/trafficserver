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

import subprocess
import sys
import tempfile
from pathlib import Path

import pytest


@pytest.fixture
def sample_hrw4u_files(tmp_path: Path) -> tuple[Path, Path, Path]:
    """Create sample hrw4u files for testing."""
    file1 = tmp_path / "test1.hrw4u"
    file1.write_text("REMAP { no-op(); }\n")

    file2 = tmp_path / "test2.hrw4u"
    file2.write_text("READ_RESPONSE { inbound.resp.X-Test = \"foo\"; }\n")

    file3 = tmp_path / "test3.hrw4u"
    file3.write_text("SEND_REQUEST { outbound.req.X-Custom = \"bar\"; }\n")

    return file1, file2, file3


def run_hrw4u(args: list[str], stdin: str | None = None) -> subprocess.CompletedProcess:
    """Run hrw4u script with given arguments."""
    script = Path("scripts/hrw4u").resolve()
    cmd = [sys.executable, str(script)] + args

    return subprocess.run(cmd, capture_output=True, text=True, input=stdin, cwd=Path.cwd())


def test_cli_single_file_to_stdout(sample_hrw4u_files: tuple[Path, Path, Path]) -> None:
    """Test compiling a single file to stdout."""
    file1, _, _ = sample_hrw4u_files

    result = run_hrw4u([str(file1)])

    assert result.returncode == 0
    assert "no-op" in result.stdout
    assert "REMAP" in result.stdout or "cond" in result.stdout


def test_cli_multiple_files_to_stdout(sample_hrw4u_files: tuple[Path, Path, Path]) -> None:
    """Test compiling multiple files to stdout with separators."""
    file1, file2, file3 = sample_hrw4u_files

    result = run_hrw4u([str(file1), str(file2), str(file3)])

    assert result.returncode == 0
    assert "# ---" in result.stdout
    assert result.stdout.count("# ---") == 2
    assert "no-op" in result.stdout
    assert "X-Test" in result.stdout
    assert "X-Custom" in result.stdout


def test_cli_stdin_to_stdout() -> None:
    """Test reading from stdin and writing to stdout."""
    input_content = "REMAP { inbound.req.X-Stdin = \"test\"; }\n"

    result = run_hrw4u([], stdin=input_content)

    assert result.returncode == 0
    assert "X-Stdin" in result.stdout


def test_cli_bulk_input_output_pairs(sample_hrw4u_files: tuple[Path, Path, Path], tmp_path: Path) -> None:
    """Test bulk compilation with input:output pairs."""
    file1, file2, _ = sample_hrw4u_files
    out1 = tmp_path / "out1.conf"
    out2 = tmp_path / "out2.conf"

    result = run_hrw4u([f"{file1}:{out1}", f"{file2}:{out2}"])

    assert result.returncode == 0
    assert out1.exists()
    assert out2.exists()
    assert "no-op" in out1.read_text()
    assert "X-Test" in out2.read_text()


def test_cli_mixed_format_error(sample_hrw4u_files: tuple[Path, Path, Path], tmp_path: Path) -> None:
    """Test that mixing formats (with and without colons) produces an error."""
    file1, file2, _ = sample_hrw4u_files
    out2 = tmp_path / "out2.conf"

    result = run_hrw4u([str(file1), f"{file2}:{out2}"])

    assert result.returncode != 0
    assert "Mixed formats not allowed" in result.stderr


def test_cli_nonexistent_input_file() -> None:
    """Test error handling for nonexistent input file."""
    result = run_hrw4u(["nonexistent_file.hrw4u"])

    assert result.returncode != 0
    assert "not found" in result.stderr


def test_cli_bulk_nonexistent_input_file(tmp_path: Path) -> None:
    """Test error handling for nonexistent input file in bulk mode."""
    out = tmp_path / "out.conf"

    result = run_hrw4u([f"nonexistent_file.hrw4u:{out}"])

    assert result.returncode != 0
    assert "not found" in result.stderr


def test_cli_ast_output(sample_hrw4u_files: tuple[Path, Path, Path]) -> None:
    """Test AST output mode."""
    file1, _, _ = sample_hrw4u_files

    result = run_hrw4u(["--ast", str(file1)])

    assert result.returncode == 0
    assert "program" in result.stdout.lower() or "(" in result.stdout


def test_cli_help_output() -> None:
    """Test help output."""
    result = run_hrw4u(["--help"])

    assert result.returncode == 0
    assert "usage:" in result.stdout.lower()
    assert "hrw4u" in result.stdout.lower()
    assert "bulk" in result.stdout.lower()


def test_u4wrh_single_file_to_stdout(tmp_path: Path) -> None:
    """Test u4wrh script with single file to stdout."""
    hrw_file = tmp_path / "test.conf"
    hrw_file.write_text('cond %{HEADER:X-Test} ="foo"\nset-header X-Response "bar"\n')

    script = Path("scripts/u4wrh").resolve()
    cmd = [sys.executable, str(script), str(hrw_file)]

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=Path.cwd())

    assert result.returncode == 0
    assert "X-Test" in result.stdout
    assert "X-Response" in result.stdout


def test_u4wrh_bulk_mode(tmp_path: Path) -> None:
    """Test u4wrh bulk compilation mode."""
    hrw1 = tmp_path / "test1.conf"
    hrw1.write_text('cond %{HEADER:X-Test} ="foo"\nset-header X-Response "bar"\n')

    hrw2 = tmp_path / "test2.conf"
    hrw2.write_text('set-status 404\n')

    out1 = tmp_path / "out1.hrw4u"
    out2 = tmp_path / "out2.hrw4u"

    script = Path("scripts/u4wrh").resolve()
    cmd = [sys.executable, str(script), f"{hrw1}:{out1}", f"{hrw2}:{out2}"]

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=Path.cwd())

    assert result.returncode == 0
    assert out1.exists()
    assert out2.exists()
    assert "X-Test" in out1.read_text()
    assert "404" in out2.read_text()
