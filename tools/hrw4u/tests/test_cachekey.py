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

import pytest

from hrw4u.cachekey import (
    CACHEKEY_STATEMENTS,
    KEY_TYPE_FLAG,
    PRESENCE_STATEMENTS,
    CacheKeyStatement,
    ConflictError,
    check_conflicts,
    merge,
    parse_assignment,
    parse_statement,
    render,
    writes_reserved_header,
)
from hrw4u.errors import SymbolResolutionError


def flags(*calls: tuple[str, list[str]]) -> list[str]:
    return merge(parse_statement(name, args) for name, args in calls)


@pytest.mark.parametrize(
    ("lhs", "reserved"),
    [
        ("inbound.req.@Hrw4u-Key-01", True),
        ("inbound.req.@hrw4u-header-X", True),
        ("outbound.req.@Hrw4u-Cookie-sid", True),
        ("inbound.req.@Hrw4uX", False),
        ("inbound.req.Hrw4u-Key-01", False),
        ("inbound.req.@X", False),
    ],
)
def test_writes_reserved_header(lhs: str, reserved: bool) -> None:
    assert writes_reserved_header(lhs) is reserved


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
            ("cache.url.pristine", '""', "Unknown cache key assignment"),
            ("cache.url.canonical", '""', "Unknown cache key assignment"),
            ("cache.key.prefix", '""', "can't be empty"),
            ("cache.key.prefix", "x", "must be a quoted string"),
            ("cache.key.prefix", "42", "must be a quoted string"),
            ("cache.key.prefix", '"{inbound.req.Host}"', "must be static"),
            ("cache.key.prefix", '"%{CLIENT-HEADER:Host}"', "must be static"),
            ("cache.key.prefix", '"a b"', "contains whitespace"),
            ("cache.key.prefix", '"a\\"b"', "contains a quote or backslash"),
        ],
    )
    def test_rejects(self, lhs: str, rhs: str, message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_assignment(lhs, rhs)

    def test_assignment_name_is_not_a_statement(self) -> None:
        with pytest.raises(SymbolResolutionError, match="Unknown cache key statement"):
            parse_statement("cache.url.query", [])

    def test_prefix_is_not_a_statement(self) -> None:
        with pytest.raises(SymbolResolutionError, match="Unknown cache key statement"):
            parse_statement("cache.key.prefix", ['"a"'])


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
        assert (
            merge(
                [
                    parse_statement("cache.key.cookies.add", ['"c"']),
                    parse_assignment("cache.url.path", '""'),
                    parse_statement("cache.url.canonical", []),
                    parse_assignment("cache.key.prefix", '"p"'),
                ]
                + [
                    parse_statement(name, args)
                    for name, args in (
                        ("cache.key.headers.add", ['"H"']),
                        ("cache.url.query.sort", []),
                        ("cache.url.query.keep", ['"k"']),
                        ("cache.url.query.remove", ['"q"']),
                        ("cache.url.pristine", []),
                    )
                ]
            )
            == expected
        )


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
            ("cache.url.pristine", ['"x"'], "takes no arguments"),
            ("cache.url.query.sort", ['"x"'], "takes no arguments"),
            ("cache.url.canonical", ['"x"'], "takes no arguments"),
            ("cache.key.headers.add", [], "takes at least one string argument"),
            ("cache.key.headers.add", ["X-Foo"], "must be a quoted string"),
            ("cache.key.headers.add", ['"a"', "42"], "must be a quoted string"),
            ("cache.key.headers.add", ['"{inbound.req.X}"'], "static"),
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
            ("cache.key.cookies.exists", ['"{inbound.req.X}"'], "static"),
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
        ],
    )
    def test_rejects(self, name: str, args: list[str], message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_statement(name, args)


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
