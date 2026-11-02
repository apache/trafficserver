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
"""The cache.* key statements and their lowering to cachekey.so flags.

This module only maps statements to flags. Where the statements may appear,
and where the run-plugin rule is emitted, is up to the visitor.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from collections.abc import Iterable, Sequence
from typing import Final

from antlr4 import ParserRuleContext

from hrw4u.errors import SymbolResolutionError

KEY_NAME: Final[str] = "cache.key"
URL_PREFIX: Final[str] = "cache.url."
HEADERS_ADD_NAME: Final[str] = "cache.key.headers.add"
PLUGIN: Final[str] = "cachekey.so"
CANONICAL_PREFIX: Final[str] = "--canonical-prefix=true"


def is_key_name(name: str) -> bool:
    """True for any cache.url.* or cache.key* name, known or not.

    cache() and cache.bypass() share the prefix but aren't key names.
    """
    return name == KEY_NAME or name.startswith((f"{KEY_NAME}.", URL_PREFIX))


def is_plugin_path(path: str) -> bool:
    """True if a run-plugin path names cachekey.so, bare or under a directory, quoted or not."""
    return path.strip('"').rsplit("/", 1)[-1] == PLUGIN


@dataclass(frozen=True, slots=True)
class CacheKeyFlag:
    """How one cache.* statement lowers to a cachekey.so flag.

    option is the flag name. A flag with a fixed_value is a switch; one without
    takes the statement's names as a comma-separated list.
    """

    option: str
    fixed_value: str | None = None
    # Written as `<name> = ""`, routed like any assignment, not a call.
    assignment: bool = False
    # cachekey.so looks the names up ignoring case, so a second spelling would add the same value twice.
    case_insensitive: bool = False

    @property
    def takes_list(self) -> bool:
        return self.fixed_value is None


# Table order is the order of the flags on the run-plugin line.
# yapf: disable
CACHEKEY_STATEMENTS: Final[dict[str, CacheKeyFlag]] = {
    "cache.url.pristine":     CacheKeyFlag("--uri-type", "pristine"),
    "cache.url.query.remove": CacheKeyFlag("--exclude-params"),
    "cache.url.query.keep":   CacheKeyFlag("--include-params"),
    "cache.url.query.sort":   CacheKeyFlag("--sort-params", "true"),
    "cache.url.query":        CacheKeyFlag("--remove-all-params", "true", assignment=True),
    "cache.url.path":         CacheKeyFlag("--remove-path", "true", assignment=True),
    HEADERS_ADD_NAME:         CacheKeyFlag("--include-headers", case_insensitive=True),
    "cache.key.cookies.add":  CacheKeyFlag("--include-cookies"),
}
# yapf: enable


@dataclass(frozen=True, slots=True)
class CacheKeyStatement:
    """One validated cache.* statement: its table name and the names it lists, unquoted.

    source is the parse node it came from, so errors found later can point at it.
    """

    name: str
    items: tuple[str, ...] = ()
    source: ParserRuleContext | None = field(default=None, compare=False, repr=False)


def parse_assignment(lhs: str, rhs: str, source: ParserRuleContext | None = None) -> CacheKeyStatement:
    """Validate `lhs = rhs`. rhs is the raw value text, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(lhs)
    if flag is None or not flag.assignment:
        raise SymbolResolutionError(lhs, f"Unknown cache key assignment: '{lhs}'")
    if rhs != '""':
        raise SymbolResolutionError(lhs, f'{lhs} can only be set to ""')
    return CacheKeyStatement(lhs, source=source)


def parse_statement(name: str, args: list[str], source: ParserRuleContext | None = None) -> CacheKeyStatement:
    """Validate one statement. args are the raw argument texts, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(name)
    if flag is None or flag.assignment:
        raise SymbolResolutionError(name, f"Unknown cache key statement: '{name}'")

    if not flag.takes_list:
        if args:
            raise SymbolResolutionError(name, f"{name}() takes no arguments")
        return CacheKeyStatement(name, source=source)

    if not args:
        raise SymbolResolutionError(name, f"{name}() takes at least one string argument")
    items = []
    for arg in args:
        if len(arg) < 2 or not (arg.startswith('"') and arg.endswith('"')):
            raise SymbolResolutionError(name, f"{name}() arguments must be quoted strings")
        item = arg[1:-1]
        # cachekey.so reads its flags once, at config load.
        if "{" in item:
            raise SymbolResolutionError(name, f"{name}() arguments must be static: plugin flags can't hold {{...}} values")
        if not item:
            raise SymbolResolutionError(name, f"{name}() has an empty name")
        # The flag value is comma-separated, so a comma would split one name into two.
        if "," in item:
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains a comma: pass each name as its own argument")
        if any(c.isspace() for c in item):
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains whitespace")
        # No header, cookie, or param name holds these; they would only mangle the quoted flag.
        if '"' in item or "\\" in item:
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains a quote or backslash")
        items.append(item)
    return CacheKeyStatement(name, tuple(items), source)


# Each pair contradicts itself on one run-plugin line: cachekey.so drops or skews one side.
CONFLICTS: Final[tuple[tuple[str, str], ...]] = (
    ("cache.url.query.keep", "cache.url.query.remove"),
    ("cache.url.query", "cache.url.query.keep"),
    ("cache.url.query", "cache.url.query.remove"),
)


class ConflictError(SymbolResolutionError):
    """Two statements on one run-plugin line contradict each other.

    statement is the later of the two, the one to point the error at.
    """

    def __init__(self, first: str, second: str, statement: CacheKeyStatement) -> None:
        super().__init__(first, f"{_as_written(first)} and {_as_written(second)} can't be used together")
        self.statement = statement


def check_conflicts(statements: Sequence[CacheKeyStatement]) -> None:
    """Raise ConflictError if statements that lower to one run-plugin contradict each other."""
    last_seen = {stmt.name: (index, stmt) for index, stmt in enumerate(statements)}
    for first, second in CONFLICTS:
        if first in last_seen and second in last_seen:
            _, later = max(last_seen[first], last_seen[second], key=lambda seen: seen[0])
            raise ConflictError(first, second, later)


def _as_written(name: str) -> str:
    return f'{name} = ""' if CACHEKEY_STATEMENTS[name].assignment else f"{name}()"


def merge(statements: Iterable[CacheKeyStatement]) -> list[str]:
    """Merge statements into one cachekey.so flag list, in table order."""
    ordered_items: dict[str, dict[str, str]] = {}
    for stmt in statements:
        items = ordered_items.setdefault(stmt.name, {})
        case_insensitive = CACHEKEY_STATEMENTS[stmt.name].case_insensitive
        for item in stmt.items:
            items.setdefault(item.lower() if case_insensitive else item, item)

    flags = [CANONICAL_PREFIX]
    for name, flag in CACHEKEY_STATEMENTS.items():
        if name in ordered_items:
            value = ",".join(ordered_items[name].values()) if flag.takes_list else flag.fixed_value
            flags.append(f"{flag.option}={value}")
    return flags


def render(flags: list[str]) -> str:
    """The run-plugin cachekey.so operator line for a merged flag list."""
    return " ".join([f'run-plugin "{PLUGIN}"', *(f'"{flag}"' for flag in flags)])
