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

import re
from dataclasses import dataclass, field
from collections.abc import Iterable, Sequence
from typing import Final

from antlr4 import ParserRuleContext

from hrw4u.errors import SymbolResolutionError

KEY_NAME: Final[str] = "cache.key"
URL_PREFIX: Final[str] = "cache.url."
HEADERS_ADD_NAME: Final[str] = "cache.key.headers.add"
PLUGIN: Final[str] = "cachekey.so"
# The lowering's own @ headers start with this, so the rules it writes can't clash with the user's.
RESERVED_HEADER_PREFIX: Final[str] = "@Hrw4u-"
RESERVED_HEADER_ERROR: Final[str] = f"{RESERVED_HEADER_PREFIX} headers are reserved for CACHEKEY"
# Consistent-hash parent selection hashes the parent selection URL if a plugin set one,
# so setting it from the same flags keeps a remap's parents when it moves to CACHEKEY.
KEY_TYPE_FLAG: Final[str] = "--key-type=cache_key,parent_selection_url"


def is_key_name(name: str) -> bool:
    """True for any cache.url.* or cache.key* name, known or not.

    cache() and cache.bypass() share the prefix but aren't key names.
    """
    return name == KEY_NAME or name.startswith((f"{KEY_NAME}.", URL_PREFIX))


def writes_reserved_header(lhs: str) -> bool:
    """True if an assignment target, such as inbound.req.@Hrw4u-Key-01, is one of the lowering's headers."""
    return f".{RESERVED_HEADER_PREFIX}".lower() in lhs.lower()


def is_plugin_path(path: str) -> bool:
    """True if a run-plugin path names cachekey.so, bare or under a directory, quoted or not."""
    return path.strip('"').rsplit("/", 1)[-1] == PLUGIN


# RFC 9110 and RFC 6265 token characters, less %, which header_rewrite reads as the start of a
# %{...} expansion: an exists() name ends up in a header_rewrite line.
_TOKEN: Final[re.Pattern[str]] = re.compile(r"[A-Za-z0-9!#$&'*+.^_`|~-]+")


@dataclass(frozen=True, slots=True)
class NameRule:
    """The names a header or cookie statement takes. Both are tokens: any other name can never match."""

    noun: str  # "header" or "cookie", for the error
    # @ headers, which REMAP sets and a client can't send.
    allows_internal: bool = False

    def accepts(self, name: str) -> bool:
        if self.allows_internal and name.startswith("@"):
            name = name[1:]
        return _TOKEN.fullmatch(name) is not None

    def reserves(self, name: str) -> bool:
        """True for one of the lowering's own @ headers, which a statement can't key on or test."""
        return self.allows_internal and name.lower().startswith(RESERVED_HEADER_PREFIX.lower())


HEADER_NAMES: Final[NameRule] = NameRule("header", allows_internal=True)
COOKIE_NAMES: Final[NameRule] = NameRule("cookie")


@dataclass(frozen=True, slots=True)
class CacheKeyFlag:
    """How one cache.* statement lowers to a cachekey.so flag.

    option is the flag name. A flag with a fixed_value is a switch. One without
    takes the assigned string if it is an assignment, else the statement's names
    as a comma-separated list.
    """

    option: str
    fixed_value: str | None = None
    # Written as `<name> = "..."`, routed like any assignment, not a call.
    is_assignment: bool = False
    # cachekey.so looks the names up ignoring case, so a second spelling would add the same value twice.
    case_insensitive: bool = False
    names: NameRule | None = None

    @property
    def is_switch(self) -> bool:
        return self.fixed_value is not None

    @property
    def takes_value(self) -> bool:
        """True for an assignment whose string is the flag value, so one run-plugin line holds one."""
        return self.is_assignment and not self.is_switch


# Table order is the order of the flags on the run-plugin line.
# yapf: disable
CACHEKEY_STATEMENTS: Final[dict[str, CacheKeyFlag]] = {
    "cache.key.prefix":       CacheKeyFlag("--static-prefix", is_assignment=True),
    "cache.url.canonical":    CacheKeyFlag("--canonical-prefix", "true"),
    "cache.url.pristine":     CacheKeyFlag("--uri-type", "pristine"),
    "cache.url.query.remove": CacheKeyFlag("--exclude-params"),
    "cache.url.query.keep":   CacheKeyFlag("--include-params"),
    "cache.url.query.sort":   CacheKeyFlag("--sort-params", "true"),
    "cache.url.query":        CacheKeyFlag("--remove-all-params", "true", is_assignment=True),
    "cache.url.path":         CacheKeyFlag("--remove-path", "true", is_assignment=True),
    HEADERS_ADD_NAME:         CacheKeyFlag("--include-headers", case_insensitive=True, names=HEADER_NAMES),
    "cache.key.cookies.add":  CacheKeyFlag("--include-cookies", names=COOKIE_NAMES),
}
# yapf: enable


@dataclass(frozen=True, slots=True)
class Presence:
    """How a cache.key.*.exists statement keys on a name being present, not on its value.

    cachekey.so only copies values, so the lowering sets an @ header to "1" when
    the name is present and keys on that header. The header is named after the
    name, not numbered, so editing other statements never changes the key.
    """

    header_prefix: str
    condition: str  # format string for the header_rewrite condition that reads the name
    names: NameRule
    # Header names ignore case, so two spellings test one header. Cookie names don't.
    case_insensitive: bool = False
    # Set for names that can be @ headers. An @ can't sit inside a header name, and
    # its own prefix keeps @X's header apart from the one for a client header X.
    internal_prefix: str | None = None

    def header(self, name: str) -> str:
        if self.internal_prefix and name.startswith("@"):
            return f"{self.internal_prefix}{name[1:]}"
        return f"{self.header_prefix}{name}"

    def cond_line(self, name: str) -> str:
        """Matches what `if inbound.req.X` or `if inbound.cookie.X` compiles to: an empty value counts as absent."""
        return f'cond {self.condition.format(name)} ="" [NOT]'


# yapf: disable
PRESENCE_STATEMENTS: Final[dict[str, Presence]] = {
    "cache.key.headers.exists": Presence("@Hrw4u-Header-", "%{{CLIENT-HEADER:{}}}", HEADER_NAMES, case_insensitive=True, internal_prefix="@Hrw4u-Internal-"),
    "cache.key.cookies.exists": Presence("@Hrw4u-Cookie-", "%{{COOKIE:{}}}", COOKIE_NAMES),
}
# yapf: enable


@dataclass(frozen=True, slots=True)
class CacheKeyStatement:
    """One validated cache.* statement: its table name and the names it lists, unquoted.

    source is the parse node it came from, so errors found later can point at it.
    order is its place in the CACHEKEY section, so a check across the shared and
    branch parts, which are kept apart, can tell which statement came later.
    """

    name: str
    items: tuple[str, ...] = ()
    source: ParserRuleContext | None = field(default=None, compare=False, repr=False)
    order: int = field(default=0, compare=False, repr=False)


def parse_assignment(lhs: str, rhs: str, source: ParserRuleContext | None = None) -> CacheKeyStatement:
    """Validate `lhs = rhs`. rhs is the raw value text, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(lhs)
    if flag is None or not flag.is_assignment:
        raise SymbolResolutionError(lhs, f"Unknown cache key assignment: '{lhs}'")

    if flag.is_switch:
        if rhs != '""':
            raise SymbolResolutionError(lhs, f'{lhs} can only be set to ""')
        return CacheKeyStatement(lhs, source=source)

    return CacheKeyStatement(lhs, (_flag_value(lhs, f"{lhs} value", rhs),), source)


def parse_statement(name: str, args: list[str], source: ParserRuleContext | None = None) -> CacheKeyStatement:
    """Validate one statement, a flag or a presence one. args are the raw argument texts, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(name)
    if (flag is None or flag.is_assignment) and name not in PRESENCE_STATEMENTS:
        raise SymbolResolutionError(name, f"Unknown cache key statement: '{name}'")

    if flag is not None and flag.is_switch:
        if args:
            raise SymbolResolutionError(name, f"{name}() takes no arguments")
        return CacheKeyStatement(name, source=source)

    if not args:
        raise SymbolResolutionError(name, f"{name}() takes at least one string argument")
    names = flag.names if flag is not None else PRESENCE_STATEMENTS[name].names

    items = []
    for arg in args:
        item = _flag_value(name, f"{name}() name", arg)
        # The flag value is comma-separated, so a comma would split one name into two.
        if "," in item:
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains a comma: pass each name as its own argument")
        if names is not None and names.reserves(item):
            raise SymbolResolutionError(name, RESERVED_HEADER_ERROR)
        if names is not None and not names.accepts(item):
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" is not a valid {names.noun} name")
        items.append(item)
    return CacheKeyStatement(name, tuple(items), source)


def _unquote(arg: str) -> str | None:
    """arg without its quotes, or None if it isn't a quoted string."""
    if len(arg) < 2 or not (arg.startswith('"') and arg.endswith('"')):
        return None
    return arg[1:-1]


def _flag_value(name: str, subject: str, text: str) -> str:
    """Unquote an argument or an assigned value, and raise if it can't sit in a cachekey.so flag.

    subject names the value in an error, as in "cache.key.prefix value".
    """
    value = _unquote(text)
    if value is None:
        raise SymbolResolutionError(name, f"{subject} must be a quoted string")
    # cachekey.so reads its flags once, at config load.
    if "{" in value:
        raise SymbolResolutionError(name, f"{subject} must be static: plugin flags can't hold {{...}} values")
    if not value:
        raise SymbolResolutionError(name, f"{subject} can't be empty")
    if problem := _mangling_reason(value):
        raise SymbolResolutionError(name, f'{subject} "{value}" {problem}')
    return value


def _mangling_reason(item: str) -> str | None:
    """Why item can't sit in a quoted run-plugin flag, or None if it can.

    No header, cookie, param name, or prefix holds these; they would only mangle the flag.
    """
    if any(c.isspace() for c in item):
        return "contains whitespace"
    if '"' in item or "\\" in item:
        return "contains a quote or backslash"
    return None


# Each pair contradicts itself on one run-plugin line: cachekey.so drops or skews one side.
CONFLICTS: Final[tuple[tuple[str, str], ...]] = (
    ("cache.url.query.keep", "cache.url.query.remove"),
    ("cache.url.query", "cache.url.query.keep"),
    ("cache.url.query", "cache.url.query.remove"),
    # With a canonical prefix, cachekey.so appends the static prefix without its separator.
    ("cache.url.canonical", "cache.key.prefix"),
)


class ConflictError(SymbolResolutionError):
    """Two statements on one run-plugin line contradict each other.

    statement is the later of the two, the one to point the error at and to name first.
    """

    def __init__(self, statement: CacheKeyStatement, earlier: CacheKeyStatement) -> None:
        super().__init__(statement.name, f"{_as_written(statement)} can't be used with {_as_written(earlier)}")
        self.statement = statement


def check_conflicts(statements: Sequence[CacheKeyStatement]) -> None:
    """Raise ConflictError if statements that lower to one run-plugin contradict each other.

    That covers the CONFLICTS pairs and two different values for one single-valued flag.
    """
    first_value: dict[str, CacheKeyStatement] = {}
    for stmt in statements:
        if CACHEKEY_STATEMENTS[stmt.name].takes_value:
            first = first_value.setdefault(stmt.name, stmt)
            if first.items != stmt.items:
                raise ConflictError(stmt, first)

    last_seen = {stmt.name: (index, stmt) for index, stmt in enumerate(statements)}
    for first, second in CONFLICTS:
        if first in last_seen and second in last_seen:
            (_, earlier), (_, later) = sorted((last_seen[first], last_seen[second]), key=lambda seen: seen[0])
            raise ConflictError(later, earlier)


def _as_written(stmt: CacheKeyStatement) -> str:
    flag = CACHEKEY_STATEMENTS[stmt.name]
    if flag.takes_value:
        return f'{stmt.name} = "{stmt.items[0]}"'
    return f'{stmt.name} = ""' if flag.is_assignment else f"{stmt.name}()"


def merge(statements: Iterable[CacheKeyStatement]) -> list[str]:
    """Merge statements into one cachekey.so flag list: KEY_TYPE_FLAG, then the rest in table order.

    Expects check_conflicts to have passed, so a single-valued flag has one value.
    """
    ordered_items: dict[str, dict[str, str]] = {}
    for stmt in statements:
        items = ordered_items.setdefault(stmt.name, {})
        case_insensitive = CACHEKEY_STATEMENTS[stmt.name].case_insensitive
        for item in stmt.items:
            items.setdefault(item.lower() if case_insensitive else item, item)

    flags = [KEY_TYPE_FLAG]
    for name, flag in CACHEKEY_STATEMENTS.items():
        if name in ordered_items:
            value = flag.fixed_value if flag.is_switch else ",".join(ordered_items[name].values())
            flags.append(f"{flag.option}={value}")
    return flags


def render(flags: list[str]) -> str:
    """The run-plugin cachekey.so operator line for a merged flag list."""
    return " ".join([f'run-plugin "{PLUGIN}"', *(f'"{flag}"' for flag in flags)])
