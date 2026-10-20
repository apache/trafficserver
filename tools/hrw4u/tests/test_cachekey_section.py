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

import pytest
import utils
from antlr4 import CommonTokenStream, InputStream
from hrw4u.states import SectionType
from u4wrh.hrw_visitor import HRWInverseVisitor
from u4wrh.u4wrhLexer import u4wrhLexer
from u4wrh.u4wrhParser import u4wrhParser

GROUP = Path("tests/data/cachekey")


@pytest.mark.cachekey
@pytest.mark.parametrize("input_file,output_file", utils.collect_output_test_files("cachekey", "hrw4u"))
def test_output_matches(input_file: Path, output_file: Path) -> None:
    """Test that hrw4u output matches expected output for CACHEKEY section test cases."""
    utils.run_output_test(input_file, output_file)


@pytest.mark.cachekey
@pytest.mark.ast
@pytest.mark.parametrize("input_file,ast_file", utils.collect_ast_test_files("cachekey"))
def test_ast_matches(input_file: Path, ast_file: Path) -> None:
    """Test that AST structure matches expected AST for CACHEKEY section test cases."""
    utils.run_ast_test(input_file, ast_file)


@pytest.mark.cachekey
@pytest.mark.invalid
@pytest.mark.parametrize("input_file", utils.collect_failing_inputs("cachekey"))
def test_invalid_inputs_fail(input_file: Path) -> None:
    """Test that invalid CACHEKEY inputs produce expected errors."""
    utils.run_failing_test(input_file)


@pytest.mark.cachekey
def test_valid_inputs_write_nothing_to_stderr(tmp_path: Path) -> None:
    """A valid CACHEKEY compiles silently: anything worth saying is an error."""
    inputs = sorted(f for f in GROUP.glob("*.input.txt") if ".fail." not in f.name)
    pairs = [f"{f.resolve()}:{(tmp_path / f.name).resolve()}" for f in inputs]
    result = subprocess.run([sys.executable, "scripts/hrw4u", *pairs], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stderr == ""


@pytest.mark.cachekey
def test_remap_hook_still_reads_back_as_remap() -> None:
    assert SectionType.CACHEKEY.hook_name == SectionType.REMAP.hook_name
    assert SectionType.from_hook("REMAP_PSEUDO_HOOK") is SectionType.REMAP


@pytest.mark.cachekey
@pytest.mark.reverse
def test_u4wrh_reads_cachekey_output_as_a_raw_run_plugin_in_remap() -> None:
    text = (GROUP / "headers-add.output.txt").read_text()
    tree = u4wrhParser(CommonTokenStream(u4wrhLexer(InputStream(text)))).program()
    hrw4u = "\n".join(HRWInverseVisitor().visit(tree))
    assert hrw4u.startswith("REMAP {")
    assert 'run-plugin("cachekey.so", "--canonical-prefix=true", "--include-headers=X-Region");' in hrw4u
    assert "CACHEKEY" not in hrw4u
