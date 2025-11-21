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
from __future__ import annotations

from functools import cached_property, lru_cache
from typing import Callable, Any
from hrw4u.debugging import Dbg
from hrw4u.states import SectionType
from hrw4u.common import SystemDefaults
from hrw4u.errors import SymbolResolutionError
import hrw4u.tables as tables
import hrw4u.types as types


class SymbolResolverBase:

    def __init__(self, debug: bool = SystemDefaults.DEFAULT_DEBUG) -> None:
        self._dbg = Dbg(debug)
        # Clear caches when debug status changes to ensure consistency
        if hasattr(self, '_condition_cache'):
            self._condition_cache.cache_clear()
        if hasattr(self, '_operator_cache'):
            self._operator_cache.cache_clear()

    # Cached table access for performance - Python 3.11+ cached_property
    @cached_property
    def _condition_map(self) -> dict[str, types.MapParams]:
        return tables.CONDITION_MAP

    @cached_property
    def _operator_map(self) -> dict[str, types.MapParams]:
        return tables.OPERATOR_MAP

    @cached_property
    def _function_map(self) -> dict[str, types.MapParams]:
        return tables.FUNCTION_MAP

    @cached_property
    def _statement_function_map(self) -> dict[str, types.MapParams]:
        return tables.STATEMENT_FUNCTION_MAP

    @cached_property
    def _reverse_resolution_map(self) -> dict[str, Any]:
        return tables.REVERSE_RESOLUTION_MAP

    def validate_section_access(self, name: str, section: SectionType | None, restricted: set[SectionType] | None) -> None:
        if section and restricted and section in restricted:
            raise SymbolResolutionError(name, f"{name} is not available in the {section.value} section")

    @lru_cache(maxsize=256)
    def _lookup_condition_cached(self, name: str) -> types.MapParams | None:
        return self._condition_map.get(name)

    @lru_cache(maxsize=256)
    def _lookup_operator_cached(self, name: str) -> types.MapParams | None:
        return self._operator_map.get(name)

    @lru_cache(maxsize=128)
    def _lookup_function_cached(self, name: str) -> types.MapParams | None:
        return self._function_map.get(name)

    @lru_cache(maxsize=128)
    def _lookup_statement_function_cached(self, name: str) -> types.MapParams | None:
        return self._statement_function_map.get(name)

    def _debug_enter(self, method_name: str, *args: Any) -> None:
        if args:
            arg_str = ', '.join(str(arg) for arg in args)
            self._dbg.enter(f"{method_name}: {arg_str}")
        else:
            self._dbg.enter(method_name)

    def _debug_exit(self, method_name: str, result: Any = None) -> None:
        if result is not None:
            self._dbg.exit(f"{method_name} => {result}")
        else:
            self._dbg.exit(method_name)

    def _debug_log(self, message: str) -> None:
        self._dbg(message)

    def _create_symbol_error(self, symbol_name: str, message: str) -> SymbolResolutionError:
        return SymbolResolutionError(symbol_name, message)

    def _handle_unknown_symbol(self, symbol_name: str, symbol_type: str) -> SymbolResolutionError:
        return self._create_symbol_error(symbol_name, f"Unknown {symbol_type}: '{symbol_name}'")

    def _handle_validation_error(self, symbol_name: str, validation_message: str) -> SymbolResolutionError:
        return self._create_symbol_error(symbol_name, validation_message)

    def find_prefix_matches(self, target: str, table: dict[str, Any]) -> list[tuple[str, Any]]:
        matches = []
        for key, value in table.items():
            if key.endswith('.') and target.startswith(key):
                matches.append((key, value))
        return matches

    def get_longest_prefix_match(self, target: str, table: dict[str, Any]) -> tuple[str, Any] | None:
        matches = self.find_prefix_matches(target, table)
        if not matches:
            return None
        matches.sort(key=lambda x: len(x[0]), reverse=True)
        return matches[0]

    def debug_context(self, method_name: str, *args: Any):

        class DebugContext:

            def __init__(self, resolver: SymbolResolverBase, method: str, arguments: tuple[Any, ...]):
                self.resolver = resolver
                self.method = method
                self.args = arguments

            def __enter__(self):
                self.resolver._debug_enter(self.method, *self.args)
                return self

            def __exit__(self, exc_type, exc_val, exc_tb):
                if exc_type is None:
                    self.resolver._debug_exit(self.method)
                else:
                    self.resolver._debug_exit(f"{self.method} (exception: {exc_type.__name__})")

        return DebugContext(self, method_name, args)
