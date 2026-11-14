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
from hrw4u.cachekey import (
    CACHEKEY_STATEMENTS,
    KEY_TYPE_FLAG,
    MAX_KEY_PARTS,
    PRESENCE_STATEMENTS,
    BranchKeyword,
    CacheKeySection,
    CacheKeyStatement,
    ConflictError,
    check_conflicts,
    merge,
    names_reserved_header,
    parse_assignment,
    parse_statement,
    render,
)
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
def test_remap_hook_reads_back_as_remap() -> None:
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


def flags(*calls: tuple[str, list[str]]) -> list[str]:
    return merge(parse_statement(name, args) for name, args in calls)


@pytest.mark.parametrize(
    ("name", "reserved"),
    [
        ("inbound.req.@Hrw4u-Key-01", True),
        ("inbound.req.@hrw4u-header-X", True),
        ("outbound.req.@Hrw4u-Cookie-sid", True),
        ("inbound.req.@Hrw4uX", False),
        ("inbound.req.Hrw4u-Key-01", False),
        ("inbound.req.@X", False),
    ],
)
def test_names_reserved_header(name: str, reserved: bool) -> None:
    assert names_reserved_header(name) is reserved


class TestParseAssignment:
    @pytest.mark.parametrize(
        "lhs,flag",
        [
            ("cache.url.query", "--remove-all-params=true"),
            ("cache.url.path", "--remove-path=true"),
        ],
    )
    def test_flag(self, lhs: str, flag: str) -> None:
        assert merge([parse_assignment(lhs, '""')]) == [KEY_TYPE_FLAG, flag]

    @pytest.mark.parametrize("value", ["assets.example.com", "assets.example.com/443", "a,b", "v2"])
    def test_prefix(self, value: str) -> None:
        stmt = parse_assignment("cache.key.prefix", f'"{value}"')
        assert stmt.items == (value,)
        assert merge([stmt]) == [KEY_TYPE_FLAG, f"--static-prefix={value}"]

    @pytest.mark.parametrize(
        "lhs,rhs,message",
        [
            ("cache.url.query", '"a=1"', 'can only be set to ""'),
            ("cache.url.path", '"/x"', 'can only be set to ""'),
            ("cache.url.query", "x", 'can only be set to ""'),
            ("cache.url.bogus", '""', "Unknown cache key assignment"),
            ("cache.url.pristine", '""', r"cache.url.pristine is a statement: write cache.url.pristine\(\)"),
            ("cache.url.canonical", '""', r"cache.url.canonical is a statement: write cache.url.canonical\(\)"),
            (
                "cache.key.headers.exists",
                '"X"',
                r"cache.key.headers.exists is a statement: write cache.key.headers.exists\(\.\.\.\)",
            ),
            ("cache.key.prefix", '""', "can't be empty"),
            ("cache.key.prefix", "x", "must be a quoted string"),
            ("cache.key.prefix", "42", "must be a quoted string"),
            (
                "cache.key.prefix",
                '"{inbound.req.Host}"',
                r"cache.key.prefix value must be static: it's fixed when the rule loads, so it can't hold \{...\} values",
            ),
            ("cache.key.prefix", '"%{CLIENT-HEADER:Host}"', "must be static"),
            ("cache.key.prefix", '"a b"', "contains whitespace"),
            ("cache.key.prefix", '"a\\"b"', "contains a quote or backslash"),
        ],
    )
    def test_rejects(self, lhs: str, rhs: str, message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_assignment(lhs, rhs)


class TestCheckConflicts:
    @pytest.mark.parametrize(
        "stmts,message",
        [
            (
                [parse_statement("cache.url.query.keep", ['"a"']), parse_statement("cache.url.query.remove", ['"b"'])],
                r"cache.url.query.remove\(\) can't be used with cache.url.query.keep\(\)",
            ),
            (
                [parse_statement("cache.url.query.remove", ['"b"']), parse_statement("cache.url.query.keep", ['"a"'])],
                r"cache.url.query.keep\(\) can't be used with cache.url.query.remove\(\)",
            ),
            (
                [parse_assignment("cache.url.query", '""'), parse_statement("cache.url.query.keep", ['"a"'])],
                r'cache.url.query.keep\(\) can\'t be used with cache.url.query = ""',
            ),
            (
                [parse_statement("cache.url.query.remove", ['"a"']), parse_assignment("cache.url.query", '""')],
                r'cache.url.query = "" can\'t be used with cache.url.query.remove\(\)',
            ),
            (
                [parse_statement("cache.url.canonical", []), parse_assignment("cache.key.prefix", '"a"')],
                r'cache.key.prefix = "a" can\'t be used with cache.url.canonical\(\)',
            ),
            (
                [parse_assignment("cache.key.prefix", '"a"'), parse_assignment("cache.key.prefix", '"b"')],
                r'cache.key.prefix = "b" can\'t be used with cache.key.prefix = "a"',
            ),
        ],
    )
    def test_rejects_and_points_at_the_later(self, stmts: list[CacheKeyStatement], message: str) -> None:
        with pytest.raises(ConflictError, match=message) as error:
            check_conflicts(stmts)
        assert error.value.statement is stmts[-1]

    def test_compatible(self) -> None:
        check_conflicts(
            [
                parse_statement("cache.url.query.remove", ['"a"']),
                parse_statement("cache.url.query.sort", []),
                parse_assignment("cache.url.path", '""'),
            ]
        )

    def test_same_prefix_twice(self) -> None:
        check_conflicts([parse_assignment("cache.key.prefix", '"a"'), parse_assignment("cache.key.prefix", '"a"')])


class TestMerge:
    def test_no_statements_sets_only_the_key_types(self) -> None:
        assert merge([]) == ["--key-type=cache_key,parent_selection_url"]

    def test_key_types_come_first(self) -> None:
        assert flags(("cache.url.pristine", []))[0] == KEY_TYPE_FLAG

    @pytest.mark.parametrize(
        "name,args,flag",
        [
            ("cache.url.canonical", [], "--canonical-prefix=true"),
            ("cache.url.pristine", [], "--uri-type=pristine"),
            ("cache.url.query.remove", ['"a"', '"b"'], "--exclude-params=a,b"),
            ("cache.url.query.keep", ['"a"', '"b"'], "--include-params=a,b"),
            ("cache.url.query.sort", [], "--sort-params=true"),
            ("cache.key.headers.add", ['"H1"', '"@H2"'], "--include-headers=H1,@H2"),
            ("cache.key.cookies.add", ['"c1"', '"c2"'], "--include-cookies=c1,c2"),
        ],
    )
    def test_one_statement(self, name: str, args: list[str], flag: str) -> None:
        assert flags((name, args)) == [KEY_TYPE_FLAG, flag]

    def test_repeats_merge_dedupe_and_keep_first_seen_order(self) -> None:
        assert flags(
            ("cache.key.headers.add", ['"B"', '"A"']),
            ("cache.key.headers.add", ['"A"', '"C"']),
        ) == [KEY_TYPE_FLAG, "--include-headers=B,A,C"]

    def test_header_names_dedupe_ignoring_case_and_keep_first_spelling(self) -> None:
        assert flags(
            ("cache.key.headers.add", ['"X-A"', '"x-a"']),
            ("cache.key.headers.add", ['"X-B"', '"x-b"']),
        ) == [KEY_TYPE_FLAG, "--include-headers=X-A,X-B"]

    def test_cookie_names_keep_case(self) -> None:
        assert flags(("cache.key.cookies.add", ['"c"', '"C"'])) == [KEY_TYPE_FLAG, "--include-cookies=c,C"]

    def test_repeated_flag_statement_is_a_no_op(self) -> None:
        assert flags(("cache.url.pristine", []), ("cache.url.pristine", [])) == [KEY_TYPE_FLAG, "--uri-type=pristine"]

    def test_table_order_not_source_order(self) -> None:
        expected = [
            KEY_TYPE_FLAG,
            "--static-prefix=p",
            "--canonical-prefix=true",
            "--uri-type=pristine",
            "--exclude-params=q",
            "--include-params=k",
            "--sort-params=true",
            "--remove-path=true",
            "--include-headers=H",
            "--include-cookies=c",
        ]
        statements = [
            parse_statement("cache.key.cookies.add", ['"c"']),
            parse_assignment("cache.url.path", '""'),
            parse_statement("cache.url.canonical", []),
            parse_assignment("cache.key.prefix", '"p"'),
            parse_statement("cache.key.headers.add", ['"H"']),
            parse_statement("cache.url.query.sort", []),
            parse_statement("cache.url.query.keep", ['"k"']),
            parse_statement("cache.url.query.remove", ['"q"']),
            parse_statement("cache.url.pristine", []),
        ]

        assert merge(statements) == expected


class TestRender:
    def test_quotes_each_flag(self) -> None:
        assert (
            render(["--static-prefix=a", "--uri-type=pristine"])
            == 'run-plugin "cachekey.so" "--static-prefix=a" "--uri-type=pristine"'
        )

    def test_no_flags(self) -> None:
        assert render([]) == 'run-plugin "cachekey.so"'

    def test_no_statements(self) -> None:
        assert render(merge([])) == 'run-plugin "cachekey.so" "--key-type=cache_key,parent_selection_url"'


class TestParseStatementErrors:
    @pytest.mark.parametrize(
        "name,args,message",
        [
            ("cache.url.bogus", [], "Unknown cache key statement"),
            ("cache.url.query", [], 'cache.url.query is an assignment: write cache.url.query = ""'),
            ("cache.key.prefix", ['"a"'], r'cache.key.prefix is an assignment: write cache.key.prefix = "\.\.\."'),
            ("cache.url.pristine", ['"x"'], "takes no arguments"),
            ("cache.url.query.sort", ['"x"'], "takes no arguments"),
            ("cache.url.canonical", ['"x"'], "takes no arguments"),
            ("cache.key.headers.add", [], "takes at least one string argument"),
            ("cache.key.headers.add", ["X-Foo"], "must be a quoted string"),
            ("cache.key.headers.add", ['"a"', "42"], "must be a quoted string"),
            (
                "cache.key.headers.add",
                ['"{inbound.req.X}"'],
                r"cache.key.headers.add\(\) name must be static: it's fixed when the rule loads, so it can't hold \{...\} values",
            ),
            ("cache.key.headers.add", ['"%{CLIENT-HEADER:X}"'], "static"),
            ("cache.key.headers.add", ['""'], "can't be empty"),
            ("cache.key.headers.add", ['"a"', '""'], "can't be empty"),
            ("cache.key.headers.add", ['"a,b"'], "contains a comma: pass each name as its own argument"),
            ("cache.url.query.keep", ['"a"', '"b,c"'], "contains a comma"),
            ("cache.key.cookies.add", ['" b"'], "whitespace"),
            ("cache.url.query.remove", ['"a b"'], "whitespace"),
            ("cache.key.headers.add", ['"a\\"b"'], "quote or backslash"),
            ("cache.key.headers.add", ['"a\\nb"'], "quote or backslash"),
            ("cache.key.headers.exists", [], "takes at least one string argument"),
            (
                "cache.key.cookies.exists",
                ['"{inbound.req.X}"'],
                r"cache.key.cookies.exists\(\) name must be static: it's fixed when the rule loads, so it can't hold \{...\} values",
            ),
            ("cache.key.headers.exists", ['"X:Y"'], "not a valid header name"),
            ("cache.key.headers.exists", ['"@"'], "not a valid header name"),
            ("cache.key.headers.exists", ['"@@X"'], "not a valid header name"),
            ("cache.key.headers.exists", ['"X@Y"'], "not a valid header name"),
            ("cache.key.cookies.exists", ['"@x"'], "not a valid cookie name"),
            ("cache.key.cookies.exists", ['"a%b"'], "not a valid cookie name"),
            ("cache.key.cookies.exists", ['"a=b"'], "not a valid cookie name"),
            ("cache.key.headers.add", ['"X-User:Id"'], "not a valid header name"),
            ("cache.key.headers.add", ['"@"'], "not a valid header name"),
            ("cache.key.cookies.add", ['"a=b"'], "not a valid cookie name"),
            ("cache.key.cookies.add", ['"@x"'], "not a valid cookie name"),
            ("cache.key.headers.add", ['"@Hrw4u-Key-01"'], "@Hrw4u- headers are reserved for the compiler"),
            ("cache.key.headers.add", ['"X"', '"@hrw4u-key-01"'], "@Hrw4u- headers are reserved for the compiler"),
            ("cache.key.headers.exists", ['"@Hrw4u-Header-X"'], "@Hrw4u- headers are reserved for the compiler"),
        ],
    )
    def test_rejects(self, name: str, args: list[str], message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_statement(name, args)

    def test_a_name_like_the_reserved_prefix_passes(self) -> None:
        assert parse_statement("cache.key.headers.add", ['"@Hrw4uX"']).items == ("@Hrw4uX",)


class TestPresence:
    @pytest.mark.parametrize("name", ["cache.key.headers.exists", "cache.key.cookies.exists"])
    def test_parses_like_a_list_statement(self, name: str) -> None:
        assert parse_statement(name, ['"a"', '"B-c"']).items == ("a", "B-c")

    @pytest.mark.parametrize(
        "name,header,cond",
        [
            ("cache.key.headers.exists", "@Hrw4u-Header-X-Id", 'cond %{CLIENT-HEADER:X-Id} ="" [NOT]'),
            ("cache.key.cookies.exists", "@Hrw4u-Cookie-X-Id", 'cond %{COOKIE:X-Id} ="" [NOT]'),
        ],
    )
    def test_header_and_condition(self, name: str, header: str, cond: str) -> None:
        presence = PRESENCE_STATEMENTS[name]
        assert presence.header("X-Id") == header
        assert presence.cond_line("X-Id") == cond

    def test_internal_header(self) -> None:
        presence = PRESENCE_STATEMENTS["cache.key.headers.exists"]
        assert parse_statement("cache.key.headers.exists", ['"@X-Id"']).items == ("@X-Id",)
        assert presence.header("@X-Id") == "@Hrw4u-Internal-X-Id"
        assert presence.cond_line("@X-Id") == 'cond %{CLIENT-HEADER:@X-Id} ="" [NOT]'

    def test_no_flag_of_its_own(self) -> None:
        assert not set(PRESENCE_STATEMENTS) & set(CACHEKEY_STATEMENTS)


def headers(*names: str) -> CacheKeyStatement:
    return parse_statement("cache.key.headers.add", [f'"{name}"' for name in names])


def keyed_on(statements: list[CacheKeyStatement]) -> list[str]:
    return [item for stmt in statements for item in stmt.items]


class TestSectionPlacement:
    def test_outside_the_chain_goes_to_shared(self) -> None:
        section = CacheKeySection()
        section.add(headers("X-A"))
        with section.branch(BranchKeyword.IF):
            section.add(headers("X-B"))
        section.add(headers("X-C"))

        assert keyed_on(section.shared.statements) == ["X-A", "X-C"]
        assert keyed_on(section.chain[0].parts.statements) == ["X-B"]

    def test_each_arm_gets_its_own_statements(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            section.add(headers("X-A"))
        with section.branch(BranchKeyword.ELSE):
            section.add(headers("X-B"))

        assert [keyed_on(branch.parts.statements) for branch in section.chain] == [["X-A"], ["X-B"]]
        assert not section.adds_else

    def test_a_chain_without_else_gets_one(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            section.add(headers("X-A"))

        assert section.adds_else

    def test_the_branch_closes_when_its_block_raises(self) -> None:
        section = CacheKeySection()
        with pytest.raises(SymbolResolutionError), section.branch(BranchKeyword.IF):
            raise SymbolResolutionError("x", "boom")
        section.add(headers("X-A"))

        assert keyed_on(section.shared.statements) == ["X-A"]


class TestSectionChainRules:
    def test_rejects_a_nested_arm(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            with pytest.raises(SymbolResolutionError, match="nested conditionals"):
                with section.branch(BranchKeyword.IF):
                    pass

    def test_rejects_a_second_chain(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            pass
        with pytest.raises(SymbolResolutionError, match="one if/elif/else chain"):
            with section.branch(BranchKeyword.IF):
                pass


class TestSectionPresence:
    def test_one_check_per_name_however_many_arms_test_it(self) -> None:
        section = CacheKeySection()
        exists = parse_statement("cache.key.headers.exists", ['"X-A"'])
        with section.branch(BranchKeyword.IF):
            section.add(exists)
        with section.branch(BranchKeyword.ELSE):
            section.add(exists)

        assert list(section.presence) == ["@hrw4u-header-x-a"]
        assert [keyed_on(branch.parts.statements) for branch in section.chain] == [["@Hrw4u-Header-X-A"]] * 2
        assert section.shared.statements == []


class TestSectionComputed:
    def test_numbers_run_across_arms(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            section.add_computed('"a"')
        with section.branch(BranchKeyword.ELSE):
            section.add_computed('"b"')

        assert [branch.parts.computed[0].header for branch in section.chain] == ["@Hrw4u-Key-01", "@Hrw4u-Key-02"]

    def test_caps_the_parts(self) -> None:
        section = CacheKeySection()
        for _ in range(MAX_KEY_PARTS):
            section.add_computed('"v"')

        with pytest.raises(SymbolResolutionError, match=f"at most {MAX_KEY_PARTS}"):
            section.add_computed('"v"')


class TestSectionConflicts:
    def test_a_shared_conflict_is_reported_once(self) -> None:
        section = CacheKeySection()
        section.add(parse_statement("cache.url.query.keep", ['"a"']))
        section.add(parse_statement("cache.url.query.remove", ['"b"']))
        with section.branch(BranchKeyword.IF):
            pass
        with section.branch(BranchKeyword.ELSE):
            pass

        assert len(section.conflicts()) == 1

    def test_a_conflict_in_each_arm_is_reported_per_arm(self) -> None:
        section = CacheKeySection()
        section.add(parse_statement("cache.url.query.keep", ['"a"']))
        for keyword in (BranchKeyword.IF, BranchKeyword.ELSE):
            with section.branch(keyword):
                section.add(parse_statement("cache.url.query.remove", ['"b"']))

        assert len(section.conflicts()) == 2

    def test_a_shared_statement_after_the_chain_is_the_later(self) -> None:
        section = CacheKeySection()
        with section.branch(BranchKeyword.IF):
            section.add(parse_statement("cache.url.query.keep", ['"a"']))
        section.add(parse_statement("cache.url.query.remove", ['"b"']))

        [error] = section.conflicts()
        assert error.statement.name == "cache.url.query.remove"
