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
"""The CACHEKEY section: its cache.* statements and their lowering to REMAP rules.

Each statement maps to a cachekey.so flag. Where the statements may appear is
up to the visitor, which collects the section into a CacheKeySection. Its
render() turns the section into rule sets on REMAP_PSEUDO_HOOK: one per presence
check, then one with one run-plugin cachekey.so per branch. The branches are
exclusive, so exactly one cachekey.so instance runs per request.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Iterator, Sequence
from contextlib import contextmanager
from dataclasses import dataclass, field, replace
from enum import Enum
from operator import attrgetter
from typing import Final

from antlr4 import ParserRuleContext

from hrw4u.common import SystemDefaults
from hrw4u.errors import SymbolResolutionError
from hrw4u.states import SectionType

KEY_NAME: Final[str] = "cache.key"
URL_PREFIX: Final[str] = "cache.url."
HEADERS_ADD_NAME: Final[str] = "cache.key.headers.add"
PLUGIN: Final[str] = "cachekey.so"
# The lowering's own @ headers start with this, so the rules it writes can't clash with the user's.
RESERVED_HEADER_PREFIX: Final[str] = "@Hrw4u-"
RESERVED_HEADER_ERROR: Final[str] = f"{RESERVED_HEADER_PREFIX} headers are reserved for the compiler"
# Consistent-hash parent selection hashes the parent selection URL if a plugin set one,
# so setting it from the same flags keeps a remap's parents when it moves to CACHEKEY.
KEY_TYPE_FLAG: Final[str] = "--key-type=cache_key,parent_selection_url"


def is_key_name(name: str) -> bool:
    """True for any cache.url.* or cache.key* name, known or not.

    cache() and cache.bypass() share the prefix but aren't key names.
    """
    return name == KEY_NAME or name.startswith((f"{KEY_NAME}.", URL_PREFIX))


def names_reserved_header(name: str) -> bool:
    """True if a header symbol, such as inbound.req.@Hrw4u-Key-01, is one of the lowering's headers."""
    return f".{RESERVED_HEADER_PREFIX}".lower() in name.lower()


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
    if (flag is not None and not flag.is_assignment) or lhs in PRESENCE_STATEMENTS:
        args = "" if flag is not None and flag.is_switch else "..."
        raise SymbolResolutionError(lhs, f"{lhs} is a statement: write {lhs}({args})")
    if flag is None:
        raise SymbolResolutionError(lhs, f"Unknown cache key assignment: '{lhs}'")

    if flag.is_switch:
        if rhs != '""':
            raise SymbolResolutionError(lhs, f'{lhs} can only be set to ""')
        return CacheKeyStatement(lhs, source=source)

    return CacheKeyStatement(lhs, (_flag_value(lhs, f"{lhs} value", rhs),), source)


def parse_statement(name: str, args: list[str], source: ParserRuleContext | None = None) -> CacheKeyStatement:
    """Validate one statement, a flag or a presence one. args are the raw argument texts, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(name)
    if flag is not None and flag.is_assignment:
        value = '""' if flag.is_switch else '"..."'
        raise SymbolResolutionError(name, f"{name} is an assignment: write {name} = {value}")
    if flag is None and name not in PRESENCE_STATEMENTS:
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
    # cachekey.so reads its flags, and header_rewrite its conditions, once, at config load.
    if "{" in value:
        raise SymbolResolutionError(
            name, f"{subject} must be static: it's fixed when the rule loads, so it can't hold {{...}} values"
        )
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


# cachekey.so sorts --include-headers as strings, so the numbers are zero-padded.
KEY_HEADER_FORMAT: Final[str] = "@Hrw4u-Key-{:02d}"
MAX_KEY_PARTS: Final[int] = 99

HOOK_LINE: Final[str] = f"cond %{{{SectionType.CACHEKEY.hook_name}}} [AND]"
INDENT: Final[str] = " " * SystemDefaults.INDENT_SPACES


@dataclass(frozen=True, slots=True)
class KeyPart:
    """A `cache.key += "..."` value, carried to cachekey.so in an @ header."""

    header: str
    value: str  # quoted HRW string, interpolations already resolved


@dataclass(slots=True)
class KeyParts:
    """What one cachekey.so instance keys on.

    statements is in source order, a computed part counted as its --include-headers name.
    computed holds the cache.key += values, each set into its header before cachekey.so runs.
    """

    statements: list[CacheKeyStatement] = field(default_factory=list)
    computed: list[KeyPart] = field(default_factory=list)

    def __add__(self, other: KeyParts) -> KeyParts:
        return KeyParts(self.statements + other.statements, self.computed + other.computed)

    def plugin_lines(self, depth: int) -> list[str]:
        """The set-header for each computed part, then the run-plugin cachekey.so line."""
        pad = INDENT * depth
        lines = [f"{pad}set-header {part.header} {part.value}" for part in self.computed]
        lines.append(pad + render(merge(self.statements)))
        return lines


@dataclass(frozen=True, slots=True)
class PresenceCheck:
    """One name a cache.key.*.exists statement tests, set into its @ header before the chain runs."""

    statement: CacheKeyStatement
    name: str

    @property
    def presence(self) -> Presence:
        return PRESENCE_STATEMENTS[self.statement.name]

    @property
    def header(self) -> str:
        return self.presence.header(self.name)

    def rule_lines(self) -> list[str]:
        return [HOOK_LINE, self.presence.cond_line(self.name), f'{INDENT}set-header {self.header} "1"']


class BranchKeyword(str, Enum):
    IF = "if"
    ELIF = "elif"
    ELSE = "else"

    @property
    def has_condition(self) -> bool:
        return self is not BranchKeyword.ELSE

    @property
    def depth(self) -> int:
        """The indent level of the arm's conditions and operators: header_rewrite nests an elif's one deeper."""
        return 2 if self is BranchKeyword.ELIF else 1


@dataclass(slots=True)
class Branch:
    """One arm of the CACHEKEY if/elif/else chain, keyed on the shared parts plus its own."""

    keyword: BranchKeyword
    cond_lines: list[str] = field(default_factory=list)  # rendered at indent 0; empty for else
    parts: KeyParts = field(default_factory=KeyParts)


@dataclass(slots=True)
class CacheKeySection:
    """The collected CACHEKEY section: parts every request keys on, and at most one chain.

    shared holds the statements outside the chain, wherever they sit. presence holds
    each tested name once, keyed by its header lowercased, wherever it is tested:
    setting a header no branch keys on is harmless, so the checks run outside the chain.
    A statement added while a branch is open goes to that branch, else to shared.
    """

    shared: KeyParts = field(default_factory=KeyParts)
    chain: list[Branch] = field(default_factory=list)
    presence: dict[str, PresenceCheck] = field(default_factory=dict)
    _open: Branch | None = field(default=None, init=False, repr=False)
    _key_part_count: int = field(default=0, init=False, repr=False)
    _statement_count: int = field(default=0, init=False, repr=False)

    @contextmanager
    def branch(self, keyword: BranchKeyword) -> Iterator[Branch]:
        """Append an arm to the chain and keep it open for the statements in its block.

        Raises on an arm inside an open one, or on an if when the section already has a chain.
        """
        if self._open is not None:
            raise SymbolResolutionError("if", "nested conditionals not allowed in CACHEKEY")
        if keyword is BranchKeyword.IF and self.chain:
            raise SymbolResolutionError("if", "CACHEKEY allows one if/elif/else chain")

        self._open = Branch(keyword)
        self.chain.append(self._open)
        try:
            yield self._open
        finally:
            self._open = None

    def add(self, statement: CacheKeyStatement) -> None:
        """Key the open branch, or every branch outside the chain, on one parsed cache.* statement."""
        if statement.name in PRESENCE_STATEMENTS:
            self._add_presence(statement)
        else:
            self._key_on(statement)

    def add_computed(self, value: str) -> None:
        """Key the open branch, or every branch, on a cache.key += value: a quoted HRW string."""
        part = self._new_key_part(value)
        self._target.computed.append(part)
        self._key_on_header(part.header)

    @property
    def _target(self) -> KeyParts:
        return self._open.parts if self._open is not None else self.shared

    def _key_on(self, statement: CacheKeyStatement) -> None:
        self._statement_count += 1
        self._target.statements.append(replace(statement, order=self._statement_count))

    def _key_on_header(self, header: str, source: ParserRuleContext | None = None) -> None:
        """Key on one of the lowering's @ headers, which a rule set before cachekey.so runs."""
        self._key_on(CacheKeyStatement(HEADERS_ADD_NAME, (header,), source))

    def _new_key_part(self, value: str) -> KeyPart:
        """Number the next key part. Numbers run across the whole section, not per branch."""
        if self._key_part_count == MAX_KEY_PARTS:
            raise SymbolResolutionError(KEY_NAME, f"CACHEKEY allows at most {MAX_KEY_PARTS} cache.key += values")
        self._key_part_count += 1
        return KeyPart(KEY_HEADER_FORMAT.format(self._key_part_count), value)

    def _add_presence(self, statement: CacheKeyStatement) -> None:
        """Test each name in a cache.key.*.exists statement, and key on its header.

        Raises if two names need one header: header names ignore case, cookie names don't.
        """
        for name in statement.items:
            check = PresenceCheck(statement, name)
            first = self.presence.setdefault(check.header.lower(), check)
            # The prefixes differ, so a clash is two spellings of one name in one statement kind.
            # Both would set one @ header, since header names ignore case.
            if first.name != name and not check.presence.case_insensitive:
                raise SymbolResolutionError(
                    statement.name,
                    f'{statement.name}("{name}") and {first.statement.name}("{first.name}"): '
                    f"the two {check.presence.names.noun}s can't be told apart in the key",
                )
            self._key_on_header(first.header, statement.source)

    @property
    def adds_else(self) -> bool:
        """True if the chain has no else, so the lowering adds one that keys on the shared parts."""
        return bool(self.chain) and self.chain[-1].keyword.has_condition

    def conflicts(self) -> list[ConflictError]:
        """The contradictions on each run-plugin line. A conflict in the shared parts is reported once, not per branch."""
        try:
            check_conflicts(self.shared.statements)
        except ConflictError as error:
            return [error]

        # The shared parts come first in each line, wherever they sit, so check in source order.
        errors = []
        for branch in self.chain:
            try:
                check_conflicts(sorted((self.shared + branch.parts).statements, key=attrgetter("order")))
            except ConflictError as error:
                errors.append(error)
        return errors

    def render(self) -> list[str]:
        """The presence rule sets, then one run-plugin cachekey.so per branch, each with the shared parts plus its own."""
        lines = [line for check in self.presence.values() for line in [*check.rule_lines(), ""]]
        if not self.chain:
            return [*lines, HOOK_LINE, *self.shared.plugin_lines(1)]

        lines.append(HOOK_LINE)
        for branch in [*self.chain, Branch(BranchKeyword.ELSE)] if self.adds_else else self.chain:
            if branch.keyword is not BranchKeyword.IF:
                lines.append(branch.keyword.value)
            depth = branch.keyword.depth
            lines += [INDENT * (depth - 1) + line for line in branch.cond_lines]
            lines += (self.shared + branch.parts).plugin_lines(depth)
        return lines
