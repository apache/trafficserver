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
from hrw4u.cachekey import CacheKeyStatement, parse_statement
from hrw4u.cachekey_section import MAX_KEY_PARTS, BranchKeyword, CacheKeySection
from hrw4u.errors import SymbolResolutionError
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
@pytest.mark.invalid
@pytest.mark.parametrize("section", ["CACHEKEY", "REMAP"])
def test_empty_section_fails_to_parse(section: str) -> None:
    result = subprocess.run([sys.executable, "scripts/hrw4u"], input=f"{section} {{\n}}\n", capture_output=True, text=True)
    assert result.returncode != 0
    assert "mismatched input '}'" in result.stderr


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
    assert 'run-plugin("cachekey.so", "--key-type=cache_key,parent_selection_url", "--include-headers=X-Region");' in hrw4u
    assert "CACHEKEY" not in hrw4u


def headers(*names: str) -> CacheKeyStatement:
    return parse_statement("cache.key.headers.add", [f'"{name}"' for name in names])


def keyed_on(statements: list[CacheKeyStatement]) -> list[str]:
    return [item for stmt in statements for item in stmt.items]


class TestPlacement:
    def test_outside_the_chain_goes_to_shared(self) -> None:
        section = CacheKeySection()
        section.add(headers("X-A"))
        with section.branch(BranchKeyword.IF, None):
            section.add(headers("X-B"))
        section.add(headers("X-C"))

        assert keyed_on(section.shared.statements) == ["X-A", "X-C"]
        assert keyed_on(section.chain[0].parts.statements) == ["X-B"]

    def test_each_arm_gets_its_own_statements(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF, None):
            section.add(headers("X-A"))
        with section.branch(BranchKeyword.ELSE, None):
            section.add(headers("X-B"))

        assert [keyed_on(branch.parts.statements) for branch in section.chain] == [["X-A"], ["X-B"]]
        assert not section.adds_else

    def test_a_chain_without_else_gets_one(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF, None):
            section.add(headers("X-A"))

        assert section.adds_else

    def test_the_branch_closes_when_its_block_raises(self) -> None:
        section = CacheKeySection()
        with pytest.raises(SymbolResolutionError), section.branch(BranchKeyword.IF, None):
            raise SymbolResolutionError("x", "boom")
        section.add(headers("X-A"))

        assert keyed_on(section.shared.statements) == ["X-A"]


class TestChainRules:
    def test_rejects_a_nested_arm(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF, None):
            with pytest.raises(SymbolResolutionError, match="nested conditionals"):
                with section.branch(BranchKeyword.IF, None):
                    pass

    def test_rejects_a_second_chain(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF, None):
            pass
        with pytest.raises(SymbolResolutionError, match="one if/elif/else chain"):
            with section.branch(BranchKeyword.IF, None):
                pass


class TestPresence:
    def test_one_check_per_name_however_many_arms_test_it(self) -> None:
        section = CacheKeySection()
        exists = parse_statement("cache.key.headers.exists", ['"X-A"'])
        with section.branch(BranchKeyword.IF, None):
            section.add(exists)
        with section.branch(BranchKeyword.ELSE, None):
            section.add(exists)

        assert list(section.presence) == ["@hrw4u-header-x-a"]
        assert [keyed_on(branch.parts.statements) for branch in section.chain] == [["@Hrw4u-Header-X-A"]] * 2
        assert section.shared.statements == []


class TestComputed:
    def test_numbers_run_across_arms(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF, None):
            section.add_computed('"a"')
        with section.branch(BranchKeyword.ELSE, None):
            section.add_computed('"b"')

        assert [branch.parts.computed[0].header for branch in section.chain] == ["@Hrw4u-Key-01", "@Hrw4u-Key-02"]

    def test_caps_the_parts(self) -> None:
        section = CacheKeySection()
        for _ in range(MAX_KEY_PARTS):
            section.add_computed('"v"')

        with pytest.raises(SymbolResolutionError, match=f"at most {MAX_KEY_PARTS}"):
            section.add_computed('"v"')


class TestConflicts:
    def test_a_shared_conflict_is_reported_once(self) -> None:
        section = CacheKeySection()
        section.add(parse_statement("cache.url.query.keep", ['"a"']))
        section.add(parse_statement("cache.url.query.remove", ['"b"']))
        with section.branch(BranchKeyword.IF, None):
            pass
        with section.branch(BranchKeyword.ELSE, None):
            pass

        assert len(section.conflicts()) == 1

    def test_a_conflict_in_each_arm_is_reported_per_arm(self) -> None:
        section = CacheKeySection()
        section.add(parse_statement("cache.url.query.keep", ['"a"']))
        for keyword in (BranchKeyword.IF, BranchKeyword.ELSE):
            with section.branch(keyword, None):
                section.add(parse_statement("cache.url.query.remove", ['"b"']))

        assert len(section.conflicts()) == 2
