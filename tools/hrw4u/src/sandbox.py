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
"""Sandbox configuration for restricting hrw4u language features."""

from __future__ import annotations

import yaml
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from hrw4u.errors import SymbolResolutionError


class SandboxDenialError(SymbolResolutionError):
    """Raised when a feature is denied by sandbox policy."""

    def __init__(self, name: str, category: str, message: str) -> None:
        super().__init__(name, f"'{name}' is denied by sandbox policy ({category})")
        self.sandbox_message = message


_VALID_DENY_KEYS = frozenset({"sections", "functions", "conditions", "operators", "language", "modifiers"})
_VALID_LANGUAGE_CONSTRUCTS = frozenset({"break", "variables", "in", "else", "elif"})
_VALID_MODIFIERS = frozenset({"AND", "OR", "NOT", "NOCASE", "PRE", "SUF", "EXT", "MID", "I", "L", "QSA"})


def _load_deny_set(deny: dict[str, Any], key: str) -> frozenset[str]:
    raw = deny.get(key)

    if raw is None:
        return frozenset()
    if not isinstance(raw, list):
        raise ValueError(f"sandbox.deny.{key} must be a list, got {type(raw).__name__}")
    return frozenset(str(item).strip() for item in raw if item)


def _is_denied(name: str, deny_set: frozenset[str]) -> bool:
    if name in deny_set:
        return True

    for entry in deny_set:
        if entry.endswith(".") and name.startswith(entry):
            return True

    return False


@dataclass(frozen=True)
class SandboxConfig:
    message: str
    denied_sections: frozenset[str]
    denied_functions: frozenset[str]
    denied_conditions: frozenset[str]
    denied_operators: frozenset[str]
    denied_language: frozenset[str]
    denied_modifiers: frozenset[str]

    @classmethod
    def load(cls, path: Path) -> SandboxConfig:
        with open(path, encoding="utf-8") as f:
            raw = yaml.safe_load(f)

        if not isinstance(raw, dict) or "sandbox" not in raw:
            raise ValueError(f"Sandbox config must have a top-level 'sandbox' key: {path}")

        sandbox = raw["sandbox"]
        if not isinstance(sandbox, dict):
            raise ValueError(f"sandbox must be a mapping: {path}")

        message = str(sandbox.get("message", "")).strip()

        deny = sandbox.get("deny", {})
        if not isinstance(deny, dict):
            raise ValueError(f"sandbox.deny must be a mapping: {path}")

        unknown_keys = set(deny.keys()) - _VALID_DENY_KEYS
        if unknown_keys:
            raise ValueError(f"Unknown keys in sandbox.deny: {', '.join(sorted(unknown_keys))}")

        language = _load_deny_set(deny, "language")
        unknown_lang = language - _VALID_LANGUAGE_CONSTRUCTS
        if unknown_lang:
            raise ValueError(
                f"Unknown language constructs: {', '.join(sorted(unknown_lang))}. "
                f"Valid: {', '.join(sorted(_VALID_LANGUAGE_CONSTRUCTS))}")

        modifiers = frozenset(s.upper() for s in _load_deny_set(deny, "modifiers"))
        unknown_mods = modifiers - _VALID_MODIFIERS
        if unknown_mods:
            raise ValueError(
                f"Unknown modifiers: {', '.join(sorted(unknown_mods))}. "
                f"Valid: {', '.join(sorted(_VALID_MODIFIERS))}")

        return cls(
            message=message,
            denied_sections=_load_deny_set(deny, "sections"),
            denied_functions=_load_deny_set(deny, "functions"),
            denied_conditions=_load_deny_set(deny, "conditions"),
            denied_operators=_load_deny_set(deny, "operators"),
            denied_language=language,
            denied_modifiers=modifiers,
        )

    @classmethod
    def empty(cls) -> SandboxConfig:
        return cls(
            message="",
            denied_sections=frozenset(),
            denied_functions=frozenset(),
            denied_conditions=frozenset(),
            denied_operators=frozenset(),
            denied_language=frozenset(),
            denied_modifiers=frozenset(),
        )

    @property
    def is_active(self) -> bool:
        return bool(
            self.denied_sections or self.denied_functions or self.denied_conditions or self.denied_operators or
            self.denied_language or self.denied_modifiers)

    def check_section(self, section_name: str) -> None:
        if _is_denied(section_name, self.denied_sections):
            raise SandboxDenialError(section_name, "section", self.message)

    def check_function(self, func_name: str) -> None:
        if _is_denied(func_name, self.denied_functions):
            raise SandboxDenialError(func_name, "function", self.message)

    def check_condition(self, condition_key: str) -> None:
        if _is_denied(condition_key, self.denied_conditions):
            raise SandboxDenialError(condition_key, "condition", self.message)

    def check_operator(self, operator_key: str) -> None:
        if _is_denied(operator_key, self.denied_operators):
            raise SandboxDenialError(operator_key, "operator", self.message)

    def check_language(self, construct: str) -> None:
        if construct in self.denied_language:
            raise SandboxDenialError(construct, "language", self.message)

    def check_modifier(self, modifier: str) -> None:
        if modifier.upper() in self.denied_modifiers:
            raise SandboxDenialError(modifier, "modifier", self.message)
