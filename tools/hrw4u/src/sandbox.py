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
from dataclasses import dataclass, fields
from pathlib import Path
from typing import Any

from hrw4u.errors import SymbolResolutionError


class SandboxDenialError(SymbolResolutionError):
    """Raised when a feature is denied by sandbox policy."""

    def __init__(self, name: str, category: str, message: str) -> None:
        super().__init__(name, f"'{name}' is denied by sandbox policy ({category})")
        self.sandbox_message = message


_VALID_CATEGORY_KEYS = frozenset({"sections", "functions", "conditions", "operators", "language", "modifiers"})
_VALID_LANGUAGE_CONSTRUCTS = frozenset({"break", "variables", "in", "else", "elif"})
_VALID_MODIFIERS = frozenset({"AND", "OR", "NOT", "NOCASE", "PRE", "SUF", "EXT", "MID", "I", "L", "QSA"})


def _load_set(data: dict[str, Any], key: str, prefix: str) -> frozenset[str]:
    raw = data.get(key)

    if raw is None:
        return frozenset()
    if not isinstance(raw, list):
        raise ValueError(f"sandbox.{prefix}.{key} must be a list, got {type(raw).__name__}")
    return frozenset(str(item).strip() for item in raw if item)


def _is_matched(name: str, name_set: frozenset[str]) -> bool:
    if name in name_set:
        return True

    for entry in name_set:
        if entry.endswith(".") and name.startswith(entry):
            return True

    return False


@dataclass(frozen=True)
class PolicySets:
    """A set of sandbox policy entries for one severity level (deny or warn)."""
    sections: frozenset[str] = frozenset()
    functions: frozenset[str] = frozenset()
    conditions: frozenset[str] = frozenset()
    operators: frozenset[str] = frozenset()
    language: frozenset[str] = frozenset()
    modifiers: frozenset[str] = frozenset()

    @classmethod
    def load(cls, data: dict[str, Any], prefix: str) -> PolicySets:
        if not isinstance(data, dict):
            raise ValueError(f"sandbox.{prefix} must be a mapping")

        unknown_keys = set(data.keys()) - _VALID_CATEGORY_KEYS
        if unknown_keys:
            raise ValueError(f"Unknown keys in sandbox.{prefix}: {', '.join(sorted(unknown_keys))}")

        language = _load_set(data, "language", prefix)
        unknown_lang = language - _VALID_LANGUAGE_CONSTRUCTS
        if unknown_lang:
            raise ValueError(
                f"Unknown language constructs in sandbox.{prefix}: {', '.join(sorted(unknown_lang))}. "
                f"Valid: {', '.join(sorted(_VALID_LANGUAGE_CONSTRUCTS))}")

        modifiers = frozenset(s.upper() for s in _load_set(data, "modifiers", prefix))
        unknown_mods = modifiers - _VALID_MODIFIERS
        if unknown_mods:
            raise ValueError(
                f"Unknown modifiers in sandbox.{prefix}: {', '.join(sorted(unknown_mods))}. "
                f"Valid: {', '.join(sorted(_VALID_MODIFIERS))}")

        return cls(
            sections=_load_set(data, "sections", prefix),
            functions=_load_set(data, "functions", prefix),
            conditions=_load_set(data, "conditions", prefix),
            operators=_load_set(data, "operators", prefix),
            language=language,
            modifiers=modifiers,
        )

    @property
    def is_active(self) -> bool:
        return any(getattr(self, f.name) for f in fields(self))


@dataclass(frozen=True)
class SandboxConfig:
    message: str
    deny: PolicySets
    warn: PolicySets

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

        deny_data = sandbox.get("deny", {})
        if not isinstance(deny_data, dict):
            raise ValueError(f"sandbox.deny must be a mapping: {path}")
        deny = PolicySets.load(deny_data, "deny")

        warn_data = sandbox.get("warn", {})
        if not isinstance(warn_data, dict):
            raise ValueError(f"sandbox.warn must be a mapping: {path}")
        warn = PolicySets.load(warn_data, "warn")

        for f in fields(PolicySets):
            overlap = getattr(deny, f.name) & getattr(warn, f.name)
            if overlap:
                raise ValueError(f"sandbox.deny.{f.name} and sandbox.warn.{f.name} overlap: {', '.join(sorted(overlap))}")

        return cls(message=message, deny=deny, warn=warn)

    @classmethod
    def empty(cls) -> SandboxConfig:
        return cls(message="", deny=PolicySets(), warn=PolicySets())

    @property
    def is_active(self) -> bool:
        return self.deny.is_active or self.warn.is_active

    def _check(self, name: str, category: str) -> str | None:
        display = category.rstrip("s")

        if _is_matched(name, getattr(self.deny, category)):
            raise SandboxDenialError(name, display, self.message)
        if _is_matched(name, getattr(self.warn, category)):
            return f"'{name}' is warned by sandbox policy ({display})"
        return None

    def check_section(self, section_name: str) -> str | None:
        return self._check(section_name, "sections")

    def check_function(self, func_name: str) -> str | None:
        return self._check(func_name, "functions")

    def check_condition(self, condition_key: str) -> str | None:
        return self._check(condition_key, "conditions")

    def check_operator(self, operator_key: str) -> str | None:
        return self._check(operator_key, "operators")

    def check_language(self, construct: str) -> str | None:
        return self._check(construct, "language")

    def check_modifier(self, modifier: str) -> str | None:
        return self._check(modifier.upper(), "modifiers")
