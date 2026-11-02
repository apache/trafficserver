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
"""The CACHEKEY section: its collected body and its lowering to REMAP rules.

The visitor collects the section into a CacheKeySection. render() turns it into
one rule set on REMAP_PSEUDO_HOOK with one run-plugin cachekey.so per branch.
The branches are exclusive, so exactly one cachekey.so instance runs per request.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Final

from antlr4 import ParserRuleContext

from hrw4u import cachekey
from hrw4u.cachekey import CacheKeyStatement, ConflictError
from hrw4u.common import SystemDefaults
from hrw4u.errors import SymbolResolutionError
from hrw4u.states import SectionType

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

    def add_computed(self, part: KeyPart) -> None:
        self.computed.append(part)
        self.statements.append(CacheKeyStatement("cache.key.headers.add", (part.header,)))

    def __add__(self, other: KeyParts) -> KeyParts:
        return KeyParts(self.statements + other.statements, self.computed + other.computed)

    def plugin_lines(self, depth: int) -> list[str]:
        """The set-header for each computed part, then the run-plugin cachekey.so line."""
        pad = INDENT * depth
        lines = [f"{pad}set-header {part.header} {part.value}" for part in self.computed]
        lines.append(pad + cachekey.render(cachekey.merge(self.statements)))
        return lines


class BranchKeyword(str, Enum):
    IF = "if"
    ELIF = "elif"
    ELSE = "else"


@dataclass(slots=True)
class Branch:
    """One arm of the CACHEKEY if/elif/else chain, keyed on the shared parts plus its own."""

    keyword: BranchKeyword
    ctx: ParserRuleContext | None = None  # None for the else the lowering adds
    cond_lines: list[str] = field(default_factory=list)  # rendered at indent 0; empty for else
    parts: KeyParts = field(default_factory=KeyParts)


@dataclass(slots=True)
class CacheKeySection:
    """The collected CACHEKEY section: parts every request keys on, and at most one chain.

    shared holds the statements outside the chain, wherever they sit.
    """

    shared: KeyParts = field(default_factory=KeyParts)
    chain: list[Branch] = field(default_factory=list)
    key_part_count: int = 0

    def new_key_part(self, value: str) -> KeyPart:
        """Number the next key part. Numbers run across the whole section, not per branch."""
        if self.key_part_count == MAX_KEY_PARTS:
            raise SymbolResolutionError(cachekey.KEY_NAME, f"CACHEKEY allows at most {MAX_KEY_PARTS} cache.key += values")
        self.key_part_count += 1
        return KeyPart(KEY_HEADER_FORMAT.format(self.key_part_count), value)

    def add_branch(self, keyword: BranchKeyword, ctx: ParserRuleContext) -> Branch:
        """Append an arm to the chain. Raises on an if when the section already has a chain."""
        if keyword is BranchKeyword.IF and self.chain:
            raise SymbolResolutionError("if", "CACHEKEY allows one if/elif/else chain")
        branch = Branch(keyword, ctx)
        self.chain.append(branch)
        return branch

    @property
    def chain_start(self) -> ParserRuleContext | None:
        """The parse node of the chain's if, or None with no chain."""
        return self.chain[0].ctx if self.chain else None

    @property
    def adds_else(self) -> bool:
        """True if the chain has no else, so the lowering adds one that keys on the shared parts."""
        return bool(self.chain) and self.chain[-1].keyword is not BranchKeyword.ELSE

    def conflicts(self) -> list[ConflictError]:
        """The contradictions on each run-plugin line. A conflict in the shared parts is reported once, not per branch."""
        try:
            cachekey.check_conflicts(self.shared.statements)
        except ConflictError as error:
            return [error]
        errors = []
        for branch in self.chain:
            try:
                cachekey.check_conflicts((self.shared + branch.parts).statements)
            except ConflictError as error:
                errors.append(error)
        return errors

    def render(self) -> list[str]:
        """The rule set: one run-plugin cachekey.so per branch, each with the shared parts plus its own."""
        if not self.chain:
            return [HOOK_LINE, *self.shared.plugin_lines(1)]

        lines = [HOOK_LINE]
        for branch in [*self.chain, Branch(BranchKeyword.ELSE)] if self.adds_else else self.chain:
            if branch.keyword is not BranchKeyword.IF:
                lines.append(branch.keyword.value)
            # header_rewrite nests an elif's conditions and operators one level deeper.
            depth = 2 if branch.keyword is BranchKeyword.ELIF else 1
            lines += [INDENT * (depth - 1) + line for line in branch.cond_lines]
            lines += (self.shared + branch.parts).plugin_lines(depth)
        return lines
