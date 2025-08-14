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
    * and_or  - False = AND (default), True = OR
    * not_    - negation
    * nocase  - case-insensitive match
    * substr  - substring mode (SUF/PRE/EXT/MID)
    * last    - if True, omit AND/OR from string form
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum, auto
from hrw4u.errors import SymbolResolutionError


class SectionType(str, Enum):
    """Valid section types for hrw4u with their corresponding hook names"""
    REMAP = "REMAP"
    SEND_REQUEST = "SEND_REQUEST"
    READ_RESPONSE = "READ_RESPONSE"
    SEND_RESPONSE = "SEND_RESPONSE"
    READ_REQUEST = "READ_REQUEST"
    PRE_REMAP = "PRE_REMAP"
    TXN_START = "TXN_START"
    TXN_CLOSE = "TXN_CLOSE"

    @property
    def hook_name(self) -> str:
        """Get the corresponding hook name for this section type"""
        hook_map = {
            SectionType.REMAP: "REMAP_PSEUDO_HOOK",
            SectionType.SEND_REQUEST: "SEND_REQUEST_HDR_HOOK",
            SectionType.READ_RESPONSE: "READ_RESPONSE_HDR_HOOK",
            SectionType.SEND_RESPONSE: "SEND_RESPONSE_HDR_HOOK",
            SectionType.READ_REQUEST: "READ_REQUEST_HDR_HOOK",
            SectionType.PRE_REMAP: "READ_REQUEST_PRE_REMAP_HOOK",
            SectionType.TXN_START: "TXN_START_HOOK",
            SectionType.TXN_CLOSE: "TXN_CLOSE_HOOK",
        }
        return hook_map[self]

    @classmethod
    def from_hook(cls, hook_name: str) -> "SectionType":
        """Get the section type from its hook name"""
        for section in cls:
            if section.hook_name == hook_name:
                return section
        raise ValueError(f"No section found for hook '{hook_name}'")


# --- Modifier Constants ---

# Condition modifiers (used in conditional expressions)
CONDITION_MODIFIERS = frozenset({"AND", "OR", "NOT", "NOCASE", "PRE", "SUF", "EXT", "MID"})

# Operator modifiers (used in operation statements)
OPERATOR_MODIFIERS = frozenset({"I", "L", "QSA"})

# All supported modifiers in the system
ALL_MODIFIERS = CONDITION_MODIFIERS | OPERATOR_MODIFIERS

# The canonical order for sorting 'with' modifiers
WITH_MODIFIER_ORDER = ["EXT", "NOCASE", "PRE", "MID", "SUF"]

# Modifiers that can be used in a 'with' clause
WITH_MODIFIERS = frozenset(WITH_MODIFIER_ORDER)


class ModifierType(str, Enum):
    """Classification of modifier types"""
    CONDITION = "CONDITION"
    OPERATOR = "OPERATOR"
    UNKNOWN = "UNKNOWN"

    @staticmethod
    def classify(modifier: str) -> "ModifierType":
        """Determine what type of modifier this is"""
        mod_upper = modifier.upper()
        if mod_upper in CONDITION_MODIFIERS:
            return ModifierType.CONDITION
        elif mod_upper in OPERATOR_MODIFIERS:
            return ModifierType.OPERATOR
        else:
            return ModifierType.UNKNOWN


class SubstringMode(str, Enum):
    NONE = "NONE"
    SUF = "SUF"
    PRE = "PRE"
    EXT = "EXT"
    MID = "MID"


@dataclass(slots=True)
class CondState:
    and_or: bool = False  # False == AND (default)
    not_: bool = False
    nocase: bool = False
    substr: SubstringMode = SubstringMode.NONE
    last: bool = False  # For condition logic (last in AND/OR chain)

    def reset(self) -> None:
        self.and_or = self.not_ = self.nocase = self.last = False
        self.substr = SubstringMode.NONE

    def copy(self) -> "CondState":
        return replace(self)

    def add_modifier(self, mod: str) -> None:
        mod_upper = mod.upper()
        if mod_upper == "AND":
            self.and_or = False
        elif mod_upper == "OR":
            self.and_or = True
        elif mod_upper == "NOT":
            self.not_ = True
        elif mod_upper == "NOCASE":
            self.nocase = True
        elif mod_upper in SubstringMode.__members__:
            if self.substr != SubstringMode.NONE:
                raise SymbolResolutionError(mod, f"Multiple substring modifiers (already {self.substr.name})")
            self.substr = SubstringMode(mod_upper)
        else:
            raise SymbolResolutionError(mod, "Unknown condition modifier")

    def to_list(self) -> list[str]:
        """Return a full list of modifiers including logical connectors"""
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

    def to_op_flags(self) -> list[str]:
        """Return only the operation-relevant flags (no logical connectors)"""
        parts: list[str] = []
        if self.nocase:
            parts.append("NOCASE")
        if self.substr is not SubstringMode.NONE:
            parts.append(self.substr.name)
        return parts

    def to_with_modifiers(self) -> list[str]:
        """Return 'with' clause modifiers in canonical order"""
        mods: list[str] = []
        if self.nocase:
            mods.append("NOCASE")
        if self.substr is not SubstringMode.NONE:
            mods.append(self.substr.name)
        mods.sort(key=lambda m: WITH_MODIFIER_ORDER.index(m) if m in WITH_MODIFIER_ORDER else 999)
        return mods

    def __str__(self) -> str:
        parts = self.to_list()
        return f"[{','.join(parts)}]" if parts else ""


@dataclass(slots=True)
class OperatorState:
    """State for operator modifiers like [I] and [L]"""
    invert: bool = False  # [I] modifier for operations like rm-destination
    last: bool = False  # [L] modifier for operations like no-op
    qsa: bool = False

    def reset(self) -> None:
        self.invert = self.last = self.qsa = False

    def copy(self) -> "OperatorState":
        return replace(self)

    def add_modifier(self, mod: str) -> None:
        mod_upper = mod.upper()
        if mod_upper == "I":
            self.invert = True
        elif mod_upper == "L":
            self.last = True
        elif mod_upper == "QSA":
            self.qsa = True
        else:
            raise SymbolResolutionError(mod, "Unknown operator modifier")

    def to_list(self) -> list[str]:
        parts: list[str] = []
        if self.invert:
            parts.append("I")
        if self.last:
            parts.append("L")
        if self.qsa:
            parts.append("QSA")
        return parts

    def to_op_flags(self) -> list[str]:
        """Return operation flags (same as to_list for OperatorState)"""
        return self.to_list()

    def __str__(self) -> str:
        parts = self.to_list()
        return f"[{','.join(parts)}]" if parts else ""
