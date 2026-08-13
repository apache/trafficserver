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
"""Replay test configuration loading and serialization."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any
import copy
import re

import yaml


class ReplayConfigError(ValueError):
    """Report invalid pytest metadata in a replay file."""


@dataclass(frozen=True)
class ReplaySpec:
    """A Proxy Verifier replay file plus its ATS test metadata."""

    path: Path
    document: dict[str, Any]
    autest: dict[str, Any]

    @property
    def description(self) -> str:
        """Return the human-readable test description."""

        key = "description" if "description" in self.autest else "summary"
        return str(self.autest[key])

    @classmethod
    def load(cls, path: Path) -> "ReplaySpec":
        """Load and minimally validate @a path without starting test programs."""

        try:
            document = yaml.safe_load(path.read_text())
        except (OSError, yaml.YAMLError) as error:
            raise ReplayConfigError(f"Could not load {path}: {error}") from error
        if not isinstance(document, dict):
            raise ReplayConfigError(f"{path} must contain a YAML mapping")
        autest = document.get("autest")
        if not isinstance(autest, dict):
            raise ReplayConfigError(f"{path} must contain an 'autest' mapping")
        if "description" not in autest and "summary" not in autest:
            raise ReplayConfigError(f"{path} is missing 'autest.description' or 'autest.summary'")
        for required in ("server", "client", "ats"):
            if required not in autest:
                raise ReplayConfigError(f"{path} is missing 'autest.{required}'")
        for process in ("server", "client", "ats"):
            if not isinstance(autest[process], dict):
                raise ReplayConfigError(f"{path}: 'autest.{process}' must be a mapping")
        return cls(path=path, document=document, autest=autest)


def merge_flat_records(records: Mapping[str, Any], defaults: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Convert legacy dotted record names to a nested records.yaml mapping."""

    result: dict[str, Any] = copy.deepcopy(dict(defaults or {}))
    for original_name, value in records.items():
        name = original_name
        for prefix in ("proxy.config.", "local.config."):
            if name.startswith(prefix):
                name = name[len(prefix):]
                break
        cursor = result
        parts = name.split(".")
        for part in parts[:-1]:
            child = cursor.setdefault(part, {})
            if not isinstance(child, dict):
                raise ReplayConfigError(f"Record {original_name} conflicts with an existing scalar")
            cursor = child
        cursor[parts[-1]] = value
    return {"records": result}


def write_yaml(path: Path, document: Any) -> None:
    """Write one ATS YAML configuration document."""

    path.write_text(yaml.safe_dump(document, sort_keys=False))


def format_plugin_entry(entry: str | Mapping[str, Any]) -> str:
    """Render one plugin.config entry."""

    if isinstance(entry, str):
        return entry
    if isinstance(entry, Mapping):
        return " ".join([str(entry["name"]), *(str(argument) for argument in entry.get("args", []))])
    raise ReplayConfigError(f"Unsupported plugin_config entry: {entry!r}")


def replace_server_ports(value: str, http_port: int, https_port: int) -> str:
    """Replace server port placeholders in an ATS configuration value."""

    return value.replace("{SERVER_HTTP_PORT}", str(http_port)).replace("{SERVER_HTTPS_PORT}", str(https_port))


def version_tuple(value: str) -> tuple[int, ...]:
    """Return the numeric portion of a program version for minimum checks."""

    match = re.search(r"\d+(?:\.\d+)+", value)
    return tuple(int(part) for part in match.group(0).split(".")) if match else ()
