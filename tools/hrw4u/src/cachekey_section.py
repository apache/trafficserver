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
from typing import Any, Final

from hrw4u import cachekey
from hrw4u.cachekey import CacheKeyStatement
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


Entry = CacheKeyStatement | KeyPart


@dataclass(slots=True)
class Branch:
    keyword: str  # "if", "elif" or "else"
    ctx: Any = None  # parse node, for error locations; None for the added else
    cond_lines: list[str] = field(default_factory=list)  # rendered at indent 0; empty for "else"
    entries: list[Entry] = field(default_factory=list)


@dataclass(slots=True)
class CacheKeySection:
    base: list[Entry] = field(default_factory=list)
    chain: list[Branch] = field(default_factory=list)
    key_part_count: int = 0

    def new_key_part(self, value: str) -> KeyPart:
        """Number the next key part. Numbers run across the whole section, not per branch."""
        if self.key_part_count == MAX_KEY_PARTS:
            raise SymbolResolutionError(cachekey.KEY_EXPR, f"CACHEKEY allows at most {MAX_KEY_PARTS} cache.key += values")
        self.key_part_count += 1
        return KeyPart(KEY_HEADER_FORMAT.format(self.key_part_count), value)

    @property
    def adds_else(self) -> bool:
        """True if the chain has no else, so the lowering adds one that keys on the base parts."""
        return bool(self.chain) and self.chain[-1].keyword != "else"

    def render(self) -> list[str]:
        """The rule set: one run-plugin cachekey.so per branch, each with the base parts plus its own."""
        if not self.chain:
            return [HOOK_LINE, *_plugin_lines(self.base, 1)]

        lines = [HOOK_LINE]
        for branch in [*self.chain, Branch("else")] if self.adds_else else self.chain:
            if branch.keyword != "if":
                lines.append(branch.keyword)
            # header_rewrite nests an elif's conditions and operators one level deeper.
            depth = 2 if branch.keyword == "elif" else 1
            lines += [INDENT * (depth - 1) + line for line in branch.cond_lines]
            lines += _plugin_lines(self.base + branch.entries, depth)
        return lines


def as_statements(entries: list[Entry]) -> list[CacheKeyStatement]:
    """The entries as cachekey.py statements: a key part is one more --include-headers name."""
    return [
        CacheKeyStatement("cache.key.headers.add", (entry.header,)) if isinstance(entry, KeyPart) else entry for entry in entries
    ]


def _plugin_lines(entries: list[Entry], depth: int) -> list[str]:
    pad = INDENT * depth
    lines = []
    for entry in entries:
        if isinstance(entry, KeyPart):
            lines.append(f"{pad}set-header {entry.header} {entry.value}")
    lines.append(pad + cachekey.render(cachekey.merge(as_statements(entries))))
    return lines
