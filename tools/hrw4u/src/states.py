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
"""State management for logical condition modifiers."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum, auto
from typing import Self
from hrw4u.errors import SymbolResolutionError
from hrw4u.interning import intern_section, intern_hook, intern_modifier


class SectionType(str, Enum):
    """Valid section types for hrw4u with their corresponding hook names"""
    REMAP = intern_section("REMAP")
    SEND_REQUEST = intern_section("SEND_REQUEST")
    READ_RESPONSE = intern_section("READ_RESPONSE")
    SEND_RESPONSE = intern_section("SEND_RESPONSE")
    READ_REQUEST = intern_section("READ_REQUEST")
    PRE_REMAP = intern_section("PRE_REMAP")
    TXN_START = intern_section("TXN_START")
    TXN_CLOSE = intern_section("TXN_CLOSE")
    VARS = intern_section("VARS")

    @property
    def hook_name(self) -> str:
        hook_map = {
            SectionType.REMAP: intern_hook("REMAP_PSEUDO_HOOK"),
            SectionType.SEND_REQUEST: intern_hook("SEND_REQUEST_HDR_HOOK"),
            SectionType.READ_RESPONSE: intern_hook("READ_RESPONSE_HDR_HOOK"),
            SectionType.SEND_RESPONSE: intern_hook("SEND_RESPONSE_HDR_HOOK"),
            SectionType.READ_REQUEST: intern_hook("READ_REQUEST_HDR_HOOK"),
            SectionType.PRE_REMAP: intern_hook("READ_REQUEST_PRE_REMAP_HOOK"),
            SectionType.TXN_START: intern_hook("TXN_START_HOOK"),
            SectionType.TXN_CLOSE: intern_hook("TXN_CLOSE_HOOK"),
            SectionType.VARS: intern_hook("VARS_SECTION"),
        }
        return hook_map[self]

    @property
    def lsp_description(self) -> str:
        if self == SectionType.VARS:
            return "This section declares variables that can be used throughout the configuration."
        return f"This section processes requests/responses at the `{self.hook_name}` hook point in ATS."

    @classmethod
    def from_hook(cls, hook_name: str) -> Self:
        for section in cls:
            if section.hook_name == hook_name:
                return section
        raise ValueError(f"No section found for hook '{hook_name}'")


CONDITION_MODIFIERS = frozenset(
    {
        intern_modifier("AND"),
        intern_modifier("OR"),
        intern_modifier("NOT"),
        intern_modifier("NOCASE"),
        intern_modifier("PRE"),
        intern_modifier("SUF"),
        intern_modifier("EXT"),
        intern_modifier("MID")
    })

OPERATOR_MODIFIERS = frozenset({intern_modifier("I"), intern_modifier("L"), intern_modifier("QSA")})

ALL_MODIFIERS = CONDITION_MODIFIERS | OPERATOR_MODIFIERS

WITH_MODIFIER_ORDER = [
    intern_modifier("EXT"),
    intern_modifier("NOCASE"),
    intern_modifier("PRE"),
    intern_modifier("MID"),
    intern_modifier("SUF")
]

WITH_MODIFIERS = frozenset(WITH_MODIFIER_ORDER)


class ModifierType(str, Enum):
    CONDITION = "CONDITION"
    OPERATOR = "OPERATOR"
    UNKNOWN = "UNKNOWN"

    @staticmethod
    def classify(modifier: str) -> Self:
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

    def copy(self) -> Self:
        return replace(self)

    def add_modifier(self, mod: str) -> None:
        mod_upper = intern_modifier(mod)  # Intern and uppercase the modifier
        if mod_upper == intern_modifier("AND"):
            self.and_or = False
        elif mod_upper == intern_modifier("OR"):
            self.and_or = True
        elif mod_upper == intern_modifier("NOT"):
            self.not_ = True
        elif mod_upper == intern_modifier("NOCASE"):
            self.nocase = True
        elif mod_upper in SubstringMode.__members__:
            if self.substr != SubstringMode.NONE:
                raise SymbolResolutionError(mod, f"Multiple substring modifiers (already {self.substr.name})")
            self.substr = SubstringMode(mod_upper)
        else:
            raise SymbolResolutionError(mod, "Unknown condition modifier")

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

    def to_with_modifiers(self) -> list[str]:
        mods: list[str] = []
        if self.nocase:
            mods.append("NOCASE")
        if self.substr is not SubstringMode.NONE:
            mods.append(self.substr.name)
        mods.sort(key=lambda m: WITH_MODIFIER_ORDER.index(m) if m in WITH_MODIFIER_ORDER else 999)
        return mods

    def render_suffix(self) -> str:
        """Render modifier suffix for condition output."""
        parts = self.to_list()
        return f" [{','.join(parts)}]" if parts else ""

    def __str__(self) -> str:
        parts = self.to_list()
        return f"[{','.join(parts)}]" if parts else ""


@dataclass(slots=True)
class OperatorState:
    invert: bool = False  # [I] modifier for operations like rm-destination
    last: bool = False  # [L] modifier for operations like no-op
    qsa: bool = False

    def reset(self) -> None:
        self.invert = self.last = self.qsa = False

    def copy(self) -> Self:
        return replace(self)

    def add_modifier(self, mod: str) -> None:
        mod_upper = intern_modifier(mod)  # Intern and uppercase the modifier
        if mod_upper == intern_modifier("I"):
            self.invert = True
        elif mod_upper == intern_modifier("L"):
            self.last = True
        elif mod_upper == intern_modifier("QSA"):
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

    def __str__(self) -> str:
        parts = self.to_list()
        return f"[{','.join(parts)}]" if parts else ""
