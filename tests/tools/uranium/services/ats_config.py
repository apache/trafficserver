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
"""Serialization of staged ATS configuration into the files ATS reads."""

from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
from typing import Any
import copy

import yaml


class ATSConfigError(ValueError):
    """Report staged ATS configuration that cannot be written."""


def merge_flat_records(records: Mapping[str, Any], defaults: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Convert legacy dotted record names to a nested records.yaml mapping."""

    result: dict[str, Any] = copy.deepcopy(dict(defaults or {}))
    for original_name, value in records.items():
        name = original_name
        for prefix in ("proxy.config.", "local.config."):
            if name.startswith(prefix):
                name = name[len(prefix) :]
                break
        cursor = result
        parts = name.split(".")
        for part in parts[:-1]:
            child = cursor.setdefault(part, {})
            if not isinstance(child, dict):
                raise ATSConfigError(f"Record {original_name} conflicts with an existing scalar")
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
    raise ATSConfigError(f"Unsupported plugin_config entry: {entry!r}")
