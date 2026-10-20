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
"""The cache.* key statements and their lowering to cachekey.so flags.

This module only maps statements to flags. Where the statements may appear,
and where the run-plugin rule is emitted, is up to the visitor.
"""

from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Iterable
from typing import Final

from hrw4u.errors import SymbolResolutionError

CANONICAL_PREFIX: Final[str] = "--canonical-prefix=true"


@dataclass(frozen=True, slots=True)
class CacheKeyFlag:
    option: str
    fixed_value: str | None = None

    @property
    def takes_list(self) -> bool:
        return self.fixed_value is None


# Table order is the order of the flags on the run-plugin line.
# yapf: disable
CACHEKEY_STATEMENTS: Final[dict[str, CacheKeyFlag]] = {
    "cache.url.pristine":     CacheKeyFlag("--uri-type", "pristine"),
    "cache.url.query.remove": CacheKeyFlag("--exclude-params"),
    "cache.url.query.keep":   CacheKeyFlag("--include-params"),
    "cache.url.query.sort":   CacheKeyFlag("--sort-params", "true"),
    "cache.key.headers.add":  CacheKeyFlag("--include-headers"),
    "cache.key.cookies.add":  CacheKeyFlag("--include-cookies"),
}
# yapf: enable


@dataclass(frozen=True, slots=True)
class CacheKeyStatement:
    name: str
    items: tuple[str, ...] = ()


def is_cachekey_statement(name: str) -> bool:
    return name in CACHEKEY_STATEMENTS


def parse_statement(name: str, args: list[str]) -> CacheKeyStatement:
    """Validate one statement. args are the raw argument texts, quotes included."""
    flag = CACHEKEY_STATEMENTS.get(name)
    if flag is None:
        raise SymbolResolutionError(name, f"Unknown cache key statement: '{name}'")

    if not flag.takes_list:
        if args:
            raise SymbolResolutionError(name, f"{name}() takes no arguments")
        return CacheKeyStatement(name)

    if not args:
        raise SymbolResolutionError(name, f"{name}() takes at least one string argument")
    items = []
    for arg in args:
        if len(arg) < 2 or not (arg.startswith('"') and arg.endswith('"')):
            raise SymbolResolutionError(name, f"{name}() arguments must be quoted strings")
        item = arg[1:-1]
        # cachekey.so reads its flags once, at config load.
        if "{" in item:
            raise SymbolResolutionError(name, f"{name}() arguments must be static: plugin flags can't hold {{...}} values")
        if not item:
            raise SymbolResolutionError(name, f"{name}() has an empty name")
        # The flag value is comma-separated, so a comma would split one name into two.
        if "," in item:
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains a comma: pass each name as its own argument")
        if any(c.isspace() for c in item):
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains whitespace")
        # No header, cookie, or param name holds these; they would only mangle the quoted flag.
        if '"' in item or "\\" in item:
            raise SymbolResolutionError(name, f"{name}() name \"{item}\" contains a quote or backslash")
        items.append(item)
    return CacheKeyStatement(name, tuple(items))


def merge(statements: Iterable[CacheKeyStatement]) -> list[str]:
    """Merge statements into one cachekey.so flag list, in table order."""
    ordered_items: dict[str, dict[str, None]] = {}
    for stmt in statements:
        ordered_items.setdefault(stmt.name, {}).update(dict.fromkeys(stmt.items))

    flags = [CANONICAL_PREFIX]
    for name, flag in CACHEKEY_STATEMENTS.items():
        if name in ordered_items:
            value = ",".join(ordered_items[name]) if flag.takes_list else flag.fixed_value
            flags.append(f"{flag.option}={value}")
    return flags


def render(flags: list[str]) -> str:
    return " ".join(['run-plugin "cachekey.so"', *(f'"{flag}"' for flag in flags)])
