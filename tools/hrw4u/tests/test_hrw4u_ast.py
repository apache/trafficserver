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
from pathlib import Path


SAMPLE = 'REMAP {\n    inbound.req.X-Foo = "bar";\n    set-debug();\n}\n'


def run_hrw4u_ast(args: list[str], stdin: str | None = None) -> subprocess.CompletedProcess:
    script = Path("scripts/hrw4u-ast").resolve()
    cmd = [sys.executable, str(script)] + args
    return subprocess.run(cmd, capture_output=True, text=True, input=stdin)


def test_default_stage_emits_ast() -> None:
    result = run_hrw4u_ast([], stdin=SAMPLE)
    assert result.returncode == 0, result.stderr
    assert "HRW4UAST" in result.stdout
    assert "Section" in result.stdout
    assert "set-debug" in result.stdout


def test_explicit_ast_stage() -> None:
    result = run_hrw4u_ast(["--stage", "ast"], stdin=SAMPLE)
    assert result.returncode == 0, result.stderr
    assert "HRW4UAST" in result.stdout


def test_cst_stage() -> None:
    result = run_hrw4u_ast(["--stage", "cst"], stdin=SAMPLE)
    assert result.returncode == 0, result.stderr
    # toStringTree produces parenthesized rule names; "program" is the start rule.
    assert "program" in result.stdout
    assert "HRW4UAST" not in result.stdout


def test_unknown_stage_errors() -> None:
    result = run_hrw4u_ast(["--stage", "bogus"], stdin=SAMPLE)
    assert result.returncode != 0
    assert "invalid choice" in result.stderr


def test_syntax_error_returns_nonzero() -> None:
    result = run_hrw4u_ast([], stdin="REMAP { this is not valid ;")
    assert result.returncode == 1
    assert "Parse failed" in result.stderr


def test_reads_from_file_argument(tmp_path: Path) -> None:
    src = tmp_path / "sample.hrw4u"
    src.write_text(SAMPLE)
    result = run_hrw4u_ast([str(src)])
    assert result.returncode == 0, result.stderr
    assert "HRW4UAST" in result.stdout
