# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements. See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Serialize structured test metadata into ATS 9.2 configuration files."""

from collections.abc import Mapping, Sequence
from typing import Any
import json

import yaml


def records_text(document: Mapping[str, Any], prefix: str = "proxy.config") -> str:
    """Render nested records as typed records.config entries.

    :param document: Nested record names mapped to scalar configuration values.
    :param prefix: Qualified prefix for the current level of record names.
    """
    lines = []
    for key, value in document.items():
        name = str(key) if str(key).startswith(("proxy.config.", "proxy.node.", "local.config.")) else f"{prefix}.{key}"
        if isinstance(value, Mapping):
            lines.append(records_text(value, name))
            continue
        kind = "INT" if isinstance(value, int) else "FLOAT" if isinstance(value, float) else "STRING"
        if isinstance(value, bool):
            value = int(value)
        if value is None:
            value = "NULL"
        lines.append(f"CONFIG {name} {kind} {value}\n")
    return "".join(lines)


def merge_records(defaults: Mapping[str, Any], overrides: Mapping[str, Any]) -> dict[str, Any]:
    """Merge records without forcing scalar names into a YAML hierarchy.

    :param defaults: Nested default values supplied by the test harness.
    :param overrides: Qualified record names supplied by the test.
    """
    result = {}

    def flatten(values: Mapping[str, Any], prefix: str) -> None:
        """Flatten one level while preserving qualified names.

        :param values: Record values at this level.
        :param prefix: Qualified parent record name.
        """
        for key, value in values.items():
            name = str(key) if str(key).startswith(("proxy.config.", "proxy.node.", "local.config.")) else f"{prefix}.{key}"
            if isinstance(value, Mapping):
                flatten(value, name)
            else:
                result[name] = value

    flatten(defaults, "proxy.config")
    flatten(overrides, "proxy.config")
    return result


def key_value_line(values: Mapping[str, Any]) -> str:
    """Serialize one configuration entry, preserving quoted values.

    :param values: Ordered configuration keys and their scalar values.
    """
    parts = []
    for key, value in values.items():
        text = str(value).lower() if isinstance(value, bool) else str(value)
        if any(char.isspace() for char in text) or '"' in text:
            text = json.dumps(text)
        parts.append(f"{key}={text}")
    return " ".join(parts)


def multicert_text(value: Mapping[str, Any] | Sequence[str]) -> str:
    """Render logical TLS entries in the branch's ssl_multicert.config format.

    :param value: Structured certificate metadata, YAML lines, or config lines.
    """
    if not isinstance(value, Mapping):
        text = "\n".join(value) + "\n"
        if not text.lstrip().startswith("ssl_multicert:"):
            return text
        value = yaml.safe_load(text)
    return "".join(key_value_line(entry) + "\n" for entry in value.get("ssl_multicert", []) or [])


def storage_text(value: Mapping[str, Any]) -> tuple[str, str]:
    """Render storage and volume files from shared replay cache metadata.

    :param value: Cache mapping containing spans and optional volume entries.
    """
    cache = value["cache"]
    spans = []
    for span in cache.get("spans", []):
        fields = [str(span["path"])]
        if "size" in span:
            fields.append(str(span["size"]))
        if "volume" in span:
            fields.append(f"volume={span['volume']}")
        spans.append(" ".join(fields))
    volumes = []
    for entry in cache.get("volumes", []):
        volume = {"volume": entry["id"], "scheme": "http"}
        for key, value in entry.items():
            if key == "id":
                continue
            if key == "size" and isinstance(value, str) and value.endswith("M"):
                value = value[:-1]
            volume["ramcache" if key == "ram_cache" else key] = value
        volumes.append(key_value_line(volume))
    return "\n".join(spans) + "\n", "\n".join(volumes) + "\n"
