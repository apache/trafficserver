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
"""
State management for logical condition modifiers.

Flags:
    • and_or  – False = AND (default), True = OR
    • not_    – negation
    • nocase  – case-insensitive match
    • substr  – substring mode (SUF/PRE/EXT/MID)
    • last    – if True, omit AND/OR from string form

The canonical string form:
    [AND,NOCASE,EXT]   [OR,NOT]   [NOCASE]   ''
"""
from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum, auto
from hrw4u.errors import SymbolResolutionError


class SubstringMode(Enum):
    NONE = auto()
    SUF = auto()
    PRE = auto()
    EXT = auto()
    MID = auto()


@dataclass(slots=True)
class CondState:
    and_or: bool = False  # False == AND (default)
    not_: bool = False
    nocase: bool = False
    substr: SubstringMode = SubstringMode.NONE
    last: bool = False

    def reset(self) -> None:
        self.and_or = self.not_ = self.nocase = self.last = False
        self.substr = SubstringMode.NONE

    def copy(self) -> "CondState":
        return replace(self)

    _SUBSTR_MAP = {
        "SUF": SubstringMode.SUF,
        "PRE": SubstringMode.PRE,
        "EXT": SubstringMode.EXT,
        "MID": SubstringMode.MID,
    }

    def add_modifier(self, mod: str) -> None:
        mod_u = mod.upper()
        if mod_u == "NOCASE":
            self.nocase = True
            return
        try:
            mode = self._SUBSTR_MAP[mod_u]
        except KeyError:
            raise SymbolResolutionError(mod, "Unknown condition modifier")

        if self.substr is not SubstringMode.NONE:
            raise SymbolResolutionError(mod, f"Multiple substring modifiers (already {self.substr.name})")
        self.substr = mode

    def to_list(self) -> list[str]:
        parts: list[str] = []
        if not self.last:
            parts.append("OR" if self.and_or else "AND")
        if self.not_:
            parts.append("NOT")
        if self.nocase:
            parts.append("NOCASE")
        if self.substr is not SubstringMode.NONE:
            parts.append(self.substr.name)
        return parts

    def __str__(self) -> str:
        parts = self.to_list()
        return f"[{','.join(parts)}]" if parts else ""
