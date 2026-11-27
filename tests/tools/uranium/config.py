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
"""Replay manifest loading and validation."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any
import copy

import yaml


class ReplayConfigError(ValueError):
    """Report invalid pytest metadata in a replay file."""


@dataclass(frozen=True)
class ReplaySpec:
    """A Proxy Verifier replay file plus its ATS test metadata."""

    path: Path
    document: dict[str, Any]
    urtest: dict[str, Any]
    variant_name: str | None = None

    @property
    def description(self) -> str:
        """Return the human-readable test description."""

        key = "description" if "description" in self.urtest else "summary"
        return str(self.urtest[key])

    @property
    def replay_path(self) -> Path:
        """Return the Proxy Verifier traffic file used by this test."""

        replay = self.urtest.get("replay")
        return self.path if replay is None else self.path.parent / str(replay)

    @classmethod
    def load(cls, path: Path) -> "ReplaySpec":
        """Load a manifest that describes exactly one replay scenario."""

        specs = cls.load_all(path)
        if len(specs) != 1:
            raise ReplayConfigError(f"{path} contains variants; use ReplaySpec.load_all()")
        return specs[0]

    @classmethod
    def load_all(cls, path: Path) -> list["ReplaySpec"]:
        """Load every independently collected variant declared by @a path."""

        try:
            document = yaml.safe_load(path.read_text())
        except (OSError, yaml.YAMLError) as error:
            raise ReplayConfigError(f"Could not load {path}: {error}") from error
        if not isinstance(document, dict):
            raise ReplayConfigError(f"{path} must contain a YAML mapping")
        urtest = document.get("urtest")
        if not isinstance(urtest, dict):
            raise ReplayConfigError(f"{path} must contain a 'urtest' mapping")
        variants = urtest.get("variants")
        if variants is None:
            return [cls._validated(path, document, urtest)]
        if not isinstance(variants, list) or not variants:
            raise ReplayConfigError(f"{path}: 'urtest.variants' must be a non-empty list")
        base = {key: value for key, value in urtest.items() if key != "variants"}
        specs = []
        names = set()
        for variant in variants:
            if not isinstance(variant, dict) or not isinstance(variant.get("name"), str):
                raise ReplayConfigError(f"{path}: every replay variant requires a string name")
            name = variant["name"]
            if name in names:
                raise ReplayConfigError(f"{path}: duplicate replay variant name: {name}")
            names.add(name)
            overlay = {key: value for key, value in variant.items() if key != "name"}
            specs.append(cls._validated(path, document, _deep_merge(base, overlay), name))
        return specs

    @classmethod
    def _validated(
        cls,
        path: Path,
        document: dict[str, Any],
        urtest: dict[str, Any],
        variant_name: str | None = None,
    ) -> "ReplaySpec":
        """Validate one fully merged scenario."""

        if "description" not in urtest and "summary" not in urtest:
            raise ReplayConfigError(f"{path} is missing 'urtest.description' or 'urtest.summary'")
        for required in ("server", "client", "ats"):
            if required not in urtest:
                raise ReplayConfigError(f"{path} is missing 'urtest.{required}'")
        for process in ("server", "client", "ats"):
            if not isinstance(urtest[process], dict):
                raise ReplayConfigError(f"{path}: 'urtest.{process}' must be a mapping")
        manual = urtest.get("manual", False)
        if not isinstance(manual, (bool, str)):
            raise ReplayConfigError(f"{path}: 'urtest.manual' must be a boolean or reason string")
        environment = urtest["ats"].get("environment", {})
        if not isinstance(environment, dict):
            raise ReplayConfigError(f"{path}: 'urtest.ats.environment' must be a mapping")
        for check in urtest["ats"].get("file_checks", []):
            allowed = {"path", "glob", "exists", "timeout", "contains", "excludes", "line_count_min", "matches"}
            if not isinstance(check, dict) or set(check) - allowed:
                raise ReplayConfigError(f"{path}: unsupported file_checks keys: {check!r}")
            if ("path" in check) == ("glob" in check):
                raise ReplayConfigError(f"{path}: file_checks needs exactly one of path or glob: {check!r}")
            for match in check.get("matches", []):
                if not isinstance(match, dict) or "expression" not in match or set(match) - {"expression", "min", "max"}:
                    raise ReplayConfigError(f"{path}: invalid file_checks match: {match!r}")
        spec = cls(path=path, document=document, urtest=urtest, variant_name=variant_name)
        if not spec.replay_path.exists():
            raise ReplayConfigError(f"{path}: replay file does not exist: {spec.replay_path}")
        return spec


def _deep_merge(base: Mapping[str, Any], overlay: Mapping[str, Any]) -> dict[str, Any]:
    """Recursively merge one replay variant over its manifest defaults."""

    result = copy.deepcopy(dict(base))
    for key, value in overlay.items():
        if isinstance(value, Mapping) and isinstance(result.get(key), Mapping):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result
