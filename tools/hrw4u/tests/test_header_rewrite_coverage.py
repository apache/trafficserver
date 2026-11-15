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
"""Every operator and condition header_rewrite accepts is reachable from hrw4u, or listed here."""

from __future__ import annotations

import re
from itertools import product
from pathlib import Path

import pytest
from hrw4u.tables import CONDITION_MAP, FUNCTION_MAP, OPERATOR_MAP, STATEMENT_FUNCTION_MAP
from hrw4u.types import VarScope, VarType

HERE = Path(__file__).resolve()
REPO = HERE.parents[3]
FACTORY = REPO / "plugins" / "header_rewrite" / "factory.cc"

# header_rewrite names hrw4u has no spelling for yet. The goal is an empty set: remove a name when hrw4u gains it.
KNOWN_GAPS = {
    "add-cookie",
    "set-cc-alg",
    "set-effective-address",
    "set-next-hop-strategy",
    "timeout-out",
}

# header_rewrite names hrw4u will never spell, and why.
NOT_APPLICABLE = {
    "DBM": "dead code in header_rewrite: ConditionDBM never produces a value",
    "INTERNAL-TXN": "alias of INTERNAL-TRANSACTION",
    "URL": "the current request URL; inbound.url. is the pristine URL and from.url./to.url. are the remap rule's",
}

# The emitters write these directly instead of looking them up in a table.
CONDITIONS_EMITTED_BY_CODE = {"GROUP", "TRUE", "FALSE"}


# Each table entry builds its object with a lambda returning this type.
ENTRY_TYPES = {"OPERATORS": "Operator", "CONDITIONS": "Condition"}


def _factory_names(table: str) -> set[str]:
    text = FACTORY.read_text()
    body = re.search(rf"\b{table}\[\] = \{{(.*?)\n\}};", text, re.DOTALL)
    assert body, f"{table} table not found in {FACTORY}"
    names = re.findall(r'^\s*\{"([^"]+)",', body.group(1), re.MULTILINE)
    entries = text.count(f"[]() -> {ENTRY_TYPES[table]} *")
    assert len(names) == entries, f"read {len(names)} names from {table} but {FACTORY.name} has {entries} entries"
    return set(names)


def _targets(maps: tuple[dict, ...]) -> list[str]:
    targets = []
    for table in maps:
        for params in table.values():
            if isinstance(params.target, str):
                targets.append(params.target)
            elif params.target:
                targets.extend(params.target)
    return targets


def _hrw4u_operators() -> set[str]:
    names = {target.split()[0] for target in _targets((OPERATOR_MAP, STATEMENT_FUNCTION_MAP))}
    names.update(f"{scope.op_prefix}-{var.op_suffix}" for scope, var in product(VarScope, VarType))
    return names


def _hrw4u_conditions() -> set[str]:
    names = {target.removeprefix("%{").removesuffix("}").split(":")[0] for target in _targets((CONDITION_MAP, FUNCTION_MAP))}
    names.update(f"{scope.cond_prefix}-{var.cond_tag}" for scope, var in product(VarScope, VarType))
    return names | CONDITIONS_EMITTED_BY_CODE


def _hrw4u_names(table: str) -> set[str]:
    if table == "OPERATORS":
        return _hrw4u_operators()
    return _hrw4u_conditions()


def test_factory_names_are_found() -> None:
    assert FACTORY.is_file(), f"header_rewrite factory not found at {FACTORY}"
    assert {"set-header", "set-status"} <= _factory_names("OPERATORS")
    assert {"STATUS", "HEADER"} <= _factory_names("CONDITIONS")


@pytest.mark.parametrize("table", ["OPERATORS", "CONDITIONS"])
def test_every_header_rewrite_name_is_reachable(table: str) -> None:
    missing = _factory_names(table) - _hrw4u_names(table) - KNOWN_GAPS - NOT_APPLICABLE.keys()
    assert not missing, (
        f"header_rewrite has {sorted(missing)} but hrw4u cannot emit them. Map them in tools/hrw4u/src/tables.py, "
        f"or add them to KNOWN_GAPS (or NOT_APPLICABLE, with a reason) in {HERE.relative_to(REPO)}."
    )


@pytest.mark.parametrize("table", ["OPERATORS", "CONDITIONS"])
def test_listed_names_are_current(table: str) -> None:
    factory = _factory_names(table)
    listed = (KNOWN_GAPS | NOT_APPLICABLE.keys()) & factory
    supported = sorted(listed & _hrw4u_names(table))
    assert not supported, f"hrw4u now emits {supported}; remove them from KNOWN_GAPS / NOT_APPLICABLE."


def test_listed_names_exist_in_header_rewrite() -> None:
    every_name = _factory_names("OPERATORS") | _factory_names("CONDITIONS")
    gone = sorted((KNOWN_GAPS | NOT_APPLICABLE.keys()) - every_name)
    assert not gone, f"header_rewrite no longer has {gone}; remove them from KNOWN_GAPS / NOT_APPLICABLE."
