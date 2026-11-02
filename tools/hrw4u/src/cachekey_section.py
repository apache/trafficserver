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

KEY_EXPR: Final[str] = "cache.key"
# cachekey.so sorts --include-headers as strings, so the numbers are zero-padded.
KEY_HEADER_FORMAT: Final[str] = "@Hrw4u-Key-{:02d}"
MAX_KEY_PARTS: Final[int] = 99

HOOK_LINE: Final[str] = f"cond %{{{SectionType.CACHEKEY.hook_name}}} [AND]"
INDENT: Final[str] = " " * SystemDefaults.INDENT_SPACES


def is_key_name(name: str) -> bool:
    """True for any cache.url.* or cache.key* name, known or not. cache() and cache.bypass() are not key names."""
    return name == KEY_EXPR or name.startswith(("cache.key.", "cache.url."))


@dataclass(frozen=True, slots=True)
class KeyPart:
    """A `cache.key += "..."` value, carried to cachekey.so in an @ header."""

    header: str
    value: str  # quoted HRW string, interpolations already resolved


Entry = CacheKeyStatement | KeyPart


@dataclass(slots=True)
class Branch:
    keyword: str  # "if", "elif" or "else"
    cond_lines: list[str]  # rendered at indent 0; empty for "else"
    ctx: Any = None
    entries: list[Entry] = field(default_factory=list)


@dataclass(slots=True)
class CacheKeySection:
    base: list[Entry] = field(default_factory=list)
    chain: list[Branch] | None = None
    key_parts: int = 0

    def new_key_part(self, value: str) -> KeyPart:
        if self.key_parts == MAX_KEY_PARTS:
            raise SymbolResolutionError(KEY_EXPR, f"CACHEKEY allows at most {MAX_KEY_PARTS} cache.key += values")
        self.key_parts += 1
        return KeyPart(KEY_HEADER_FORMAT.format(self.key_parts), value)

    @property
    def adds_else(self) -> bool:
        """True if the chain has no else, so the lowering adds one that keys on the base parts."""
        return bool(self.chain) and self.chain[-1].keyword != "else"

    def branches(self) -> list[Branch]:
        """The chain, closed with an else when it lacks one, so every request gets a key."""
        chain = list(self.chain or [])
        if self.adds_else:
            chain.append(Branch("else", []))
        return chain

    def render(self) -> list[str]:
        lines = [HOOK_LINE]
        branches = self.branches()
        if not branches:
            return lines + _plugin_lines(self.base, 1)

        for branch in branches:
            entries = self.base + branch.entries
            if branch.keyword == "if":
                lines += branch.cond_lines
                lines += _plugin_lines(entries, 1)
            elif branch.keyword == "elif":
                lines.append("elif")
                lines += [INDENT + line for line in branch.cond_lines]
                lines += _plugin_lines(entries, 2)
            else:
                lines.append("else")
                lines += _plugin_lines(entries, 1)
        return lines


def statements(entries: list[Entry]) -> list[CacheKeyStatement]:
    """The entries as core statements: a key part is one more --include-headers name."""
    return [
        CacheKeyStatement("cache.key.headers.add", (entry.header,)) if isinstance(entry, KeyPart) else entry for entry in entries
    ]


def _plugin_lines(entries: list[Entry], depth: int) -> list[str]:
    pad = INDENT * depth
    lines = []
    for entry in entries:
        if isinstance(entry, KeyPart):
            lines.append(f"{pad}set-header {entry.header} {entry.value}")
    lines.append(pad + cachekey.render(cachekey.merge(statements(entries))))
    return lines
