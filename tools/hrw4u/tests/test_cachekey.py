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

from hrw4u.cachekey import check_conflicts, merge, parse_assignment, parse_statement, render
from hrw4u.errors import SymbolResolutionError


def flags(*calls: tuple[str, list[str]]) -> list[str]:
    return merge(parse_statement(name, args) for name, args in calls)


class TestParseAssignment:
    @pytest.mark.parametrize(
        "lhs,flag",
        [
            ("cache.url.query", "--remove-all-params=true"),
            ("cache.url.path", "--remove-path=true"),
        ],
    )
    def test_flag(self, lhs: str, flag: str) -> None:
        assert merge([parse_assignment(lhs, '""')]) == ["--canonical-prefix=true", flag]

    @pytest.mark.parametrize(
        "lhs,rhs,message",
        [
            ("cache.url.query", '"a=1"', 'can only be set to ""'),
            ("cache.url.path", '"/x"', 'can only be set to ""'),
            ("cache.url.query", "x", 'can only be set to ""'),
            ("cache.url.pristine", '""', "Unknown cache key assignment"),
        ],
    )
    def test_rejects(self, lhs: str, rhs: str, message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_assignment(lhs, rhs)

    def test_assignment_name_is_not_a_statement(self) -> None:
        with pytest.raises(SymbolResolutionError, match="Unknown cache key statement"):
            parse_statement("cache.url.query", [])


class TestCheckConflicts:
    @pytest.mark.parametrize(
        "stmts,message",
        [
            (
                [parse_statement("cache.url.query.keep", ['"a"']), parse_statement("cache.url.query.remove", ['"b"'])],
                r"cache.url.query.keep\(\) and cache.url.query.remove\(\)",
            ),
            (
                [parse_assignment("cache.url.query", '""'), parse_statement("cache.url.query.keep", ['"a"'])],
                r'cache.url.query = "" and cache.url.query.keep\(\)',
            ),
            (
                [parse_statement("cache.url.query.remove", ['"a"']), parse_assignment("cache.url.query", '""')],
                r'cache.url.query = "" and cache.url.query.remove\(\)',
            ),
        ],
    )
    def test_rejects(self, stmts: list, message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            check_conflicts(stmts)

    def test_compatible(self) -> None:
        check_conflicts(
            [
                parse_statement("cache.url.query.remove", ['"a"']),
                parse_statement("cache.url.query.sort", []),
                parse_assignment("cache.url.path", '""'),
            ]
        )


class TestMerge:
    def test_no_statements_still_has_canonical_prefix(self) -> None:
        assert merge([]) == ["--canonical-prefix=true"]

    @pytest.mark.parametrize(
        "name,args,flag",
        [
            ("cache.url.pristine", [], "--uri-type=pristine"),
            ("cache.url.query.remove", ['"a"', '"b"'], "--exclude-params=a,b"),
            ("cache.url.query.keep", ['"a"', '"b"'], "--include-params=a,b"),
            ("cache.url.query.sort", [], "--sort-params=true"),
            ("cache.key.headers.add", ['"H1"', '"@H2"'], "--include-headers=H1,@H2"),
            ("cache.key.cookies.add", ['"c1"', '"c2"'], "--include-cookies=c1,c2"),
        ],
    )
    def test_one_statement(self, name: str, args: list[str], flag: str) -> None:
        assert flags((name, args)) == ["--canonical-prefix=true", flag]

    def test_repeats_merge_dedupe_and_keep_first_seen_order(self) -> None:
        assert flags(
            ("cache.key.headers.add", ['"B"', '"A"']),
            ("cache.key.headers.add", ['"A"', '"C"']),
        ) == ["--canonical-prefix=true", "--include-headers=B,A,C"]

    def test_header_names_dedupe_ignoring_case_and_keep_first_spelling(self) -> None:
        assert flags(
            ("cache.key.headers.add", ['"X-A"', '"x-a"']),
            ("cache.key.headers.add", ['"X-B"', '"x-b"']),
        ) == ["--canonical-prefix=true", "--include-headers=X-A,X-B"]

    def test_cookie_names_keep_case(self) -> None:
        assert flags(("cache.key.cookies.add", ['"c"', '"C"'])) == ["--canonical-prefix=true", "--include-cookies=c,C"]

    def test_repeated_flag_statement_is_a_no_op(self) -> None:
        assert flags(("cache.url.pristine", []), ("cache.url.pristine", [])) == ["--canonical-prefix=true", "--uri-type=pristine"]

    def test_table_order_not_source_order(self) -> None:
        expected = [
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

    def test_client_api(self) -> None:
        assert flags(
            ("cache.url.pristine", []),
            (
                "cache.url.query.remove",
                [
                    '"utm_source"',
                    '"guid"',
                    '"utm_medium"',
                    '"utm_campaign"',
                    '"utm_term"',
                    '"client_id"',
                    '"ref"',
                    '"token"',
                    '"ts"',
                ],
            ),
            ("cache.key.headers.add", ['"X-Region"', '"@Geo-Country"']),
            ("cache.key.cookies.add", ['"session"', '"locale"', '"theme"', '"variant"']),
        ) == [
            "--canonical-prefix=true",
            "--uri-type=pristine",
            "--exclude-params=utm_source,guid,utm_medium,utm_campaign,utm_term,client_id,ref,token,ts",
            "--include-headers=X-Region,@Geo-Country",
            "--include-cookies=session,locale,theme,variant",
        ]


class TestRender:
    def test_quotes_each_flag(self) -> None:
        assert (
            render(["--canonical-prefix=true", "--uri-type=pristine"])
            == 'run-plugin "cachekey.so" "--canonical-prefix=true" "--uri-type=pristine"'
        )


class TestParseStatementErrors:
    @pytest.mark.parametrize(
        "name,args,message",
        [
            ("cache.url.bogus", [], "Unknown cache key statement"),
            ("cache.url.pristine", ['"x"'], "takes no arguments"),
            ("cache.url.query.sort", ['"x"'], "takes no arguments"),
            ("cache.key.headers.add", [], "takes at least one string argument"),
            ("cache.key.headers.add", ["X-Foo"], "must be quoted strings"),
            ("cache.key.headers.add", ['"a"', "42"], "must be quoted strings"),
            ("cache.key.headers.add", ['"{inbound.req.X}"'], "static"),
            ("cache.key.headers.add", ['"%{CLIENT-HEADER:X}"'], "static"),
            ("cache.key.headers.add", ['""'], "empty name"),
            ("cache.key.headers.add", ['"a"', '""'], "empty name"),
            ("cache.key.headers.add", ['"a,b"'], "contains a comma: pass each name as its own argument"),
            ("cache.url.query.keep", ['"a"', '"b,c"'], "contains a comma"),
            ("cache.key.cookies.add", ['" b"'], "whitespace"),
            ("cache.url.query.remove", ['"a b"'], "whitespace"),
            ("cache.key.headers.add", ['"a\\"b"'], "quote or backslash"),
            ("cache.key.headers.add", ['"a\\nb"'], "quote or backslash"),
        ],
    )
    def test_rejects(self, name: str, args: list[str], message: str) -> None:
        with pytest.raises(SymbolResolutionError, match=message):
            parse_statement(name, args)
