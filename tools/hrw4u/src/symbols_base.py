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
"""
Base class for symbol resolution with shared caching and validation logic.

This module provides the foundational functionality for both forward (HRW4U -> ATS)
and inverse (ATS -> HRW4U) symbol resolution, eliminating duplication and providing
consistent patterns for performance optimization and error handling.
"""

from __future__ import annotations

from functools import cached_property, lru_cache
from typing import Callable, Any, Set
from hrw4u.debugging import Dbg
from hrw4u.states import SectionType
from hrw4u.common import SystemDefaults
from hrw4u.errors import SymbolResolutionError
import hrw4u.tables as tables
import hrw4u.types as types


class SectionValidator:
    """Common section validation logic - moved from common.py since only used here"""

    @staticmethod
    def validate_section_access(name: str, section: SectionType | None, restricted: Set[SectionType] | None) -> None:
        """Validate if a symbol can be used in the given section."""
        if section and restricted and section in restricted:
            raise SymbolResolutionError(name, f"{name} is not available in the {section.value} section")


class SymbolResolverBase:
    """
    Base class for symbol resolution with shared caching and validation.
    """

    def __init__(self, debug: bool = SystemDefaults.DEFAULT_DEBUG) -> None:
        self._dbg = Dbg(debug)
        # Clear caches when debug status changes to ensure consistency
        if hasattr(self, '_condition_cache'):
            self._condition_cache.cache_clear()
        if hasattr(self, '_operator_cache'):
            self._operator_cache.cache_clear()

    # Cached table access for performance - Python 3.11+ cached_property
    @cached_property
    def _condition_map(
            self) -> dict[str, tuple[str, Callable[[str], None] | None, bool, set[SectionType] | None, bool, dict | None]]:
        """Cached access to condition map for performance."""
        return tables.CONDITION_MAP

    @cached_property
    def _operator_map(
            self
    ) -> dict[str, tuple[str | list[str] | tuple[str, ...], Callable[[str], None] | None, bool, set[SectionType] | None]]:
        """Cached access to operator map for performance."""
        return tables.OPERATOR_MAP

    @cached_property
    def _function_map(self) -> dict[str, tuple[str, Callable[[list[str]], None] | None]]:
        """Cached access to function map for performance."""
        return tables.FUNCTION_MAP

    @cached_property
    def _statement_function_map(self) -> dict[str, tuple[str, Callable[[list[str]], None] | None]]:
        """Cached access to statement function map for performance."""
        return tables.STATEMENT_FUNCTION_MAP

    @cached_property
    def _reverse_resolution_map(self) -> dict[str, Any]:
        """Cached access to reverse resolution map for performance."""
        return tables.REVERSE_RESOLUTION_MAP

    # Validation methods - shared across resolvers
    def validate_section_access(self, name: str, section: SectionType | None, restricted: set[SectionType] | None) -> None:
        """
        Validate that a symbol can be used in the given section.
        """
        SectionValidator.validate_section_access(name, section, restricted)

    @lru_cache(maxsize=256)
    def _lookup_condition_cached(
            self, name: str) -> tuple[str, Callable[[str], None] | None, bool, set[SectionType] | None, bool, dict | None] | None:
        """
        Cached condition lookup for performance.
        """
        return self._condition_map.get(name)

    @lru_cache(maxsize=256)
    def _lookup_operator_cached(
            self, name: str
    ) -> tuple[str | list[str] | tuple[str, ...], Callable[[str], None] | None, bool, set[SectionType] | None] | None:
        """
        Cached operator lookup for performance.
        """
        return self._operator_map.get(name)

    @lru_cache(maxsize=128)
    def _lookup_function_cached(self, name: str) -> tuple[str, Callable[[list[str]], None] | None] | None:
        """
        Cached function lookup for performance.
        """
        return self._function_map.get(name)

    @lru_cache(maxsize=128)
    def _lookup_statement_function_cached(self, name: str) -> tuple[str, Callable[[list[str]], None] | None] | None:
        """
        Cached statement function lookup for performance.
        """
        return self._statement_function_map.get(name)

    # Common debugging patterns
    def _debug_enter(self, method_name: str, *args: Any) -> None:
        """Standard debug entry pattern."""
        if args:
            arg_str = ', '.join(str(arg) for arg in args)
            self._dbg.enter(f"{method_name}: {arg_str}")
        else:
            self._dbg.enter(method_name)

    def _debug_exit(self, method_name: str, result: Any = None) -> None:
        """Standard debug exit pattern."""
        if result is not None:
            self._dbg.exit(f"{method_name} => {result}")
        else:
            self._dbg.exit(method_name)

    def _debug_log(self, message: str) -> None:
        """Standard debug logging."""
        self._dbg(message)

    # Common error handling patterns
    def _create_symbol_error(self, symbol_name: str, message: str) -> SymbolResolutionError:
        """Create a standardized SymbolResolutionError."""
        return SymbolResolutionError(symbol_name, message)

    def _handle_unknown_symbol(self, symbol_name: str, symbol_type: str) -> SymbolResolutionError:
        """Handle unknown symbol errors with consistent messaging."""
        return self._create_symbol_error(symbol_name, f"Unknown {symbol_type}: '{symbol_name}'")

    def _handle_validation_error(self, symbol_name: str, validation_message: str) -> SymbolResolutionError:
        """Handle validation errors with consistent messaging."""
        return self._create_symbol_error(symbol_name, validation_message)

    # Prefix matching utilities - shared pattern in both resolvers
    def find_prefix_matches(self, target: str, table: dict[str, Any]) -> list[tuple[str, Any]]:
        """
        Find all prefix matches for a target string in a table.
        """
        matches = []
        for key, value in table.items():
            if key.endswith('.') and target.startswith(key):
                matches.append((key, value))
        return matches

    def get_longest_prefix_match(self, target: str, table: dict[str, Any]) -> tuple[str, Any] | None:
        """
        Get the longest prefix match for a target string.
        """
        matches = self.find_prefix_matches(target, table)
        if not matches:
            return None
        matches.sort(key=lambda x: len(x[0]), reverse=True)
        return matches[0]

    # Variable type utilities - shared between resolvers
    @staticmethod
    def get_var_type_by_name(type_name: str) -> types.VarType:
        """Get VarType enum by name string with error handling."""
        try:
            return types.VarType.from_str(type_name)
        except ValueError:
            raise SymbolResolutionError(type_name, f"Invalid variable type: '{type_name}'")

    @staticmethod
    def get_var_type_by_tag(tag: str) -> types.VarType | None:
        """Get VarType enum by condition tag."""
        for var_type in types.VarType:
            if var_type.cond_tag == tag:
                return var_type
        return None

    @staticmethod
    def get_var_type_by_op_tag(op_tag: str) -> types.VarType | None:
        """Get VarType enum by operator tag."""
        for var_type in types.VarType:
            if var_type.op_tag == op_tag:
                return var_type
        return None

    # Section validation helpers
    def is_section_restricted(self, section: SectionType | None, restricted_sections: set[SectionType] | None) -> bool:
        """Check if a section is in the restricted set."""
        return bool(section and restricted_sections and section in restricted_sections)

    def get_section_restriction_error(
            self, symbol_name: str, section: SectionType, restricted_sections: set[SectionType]) -> SymbolResolutionError:
        """Create a section restriction error with helpful messaging."""
        section_names = [s.value for s in restricted_sections]
        return self._create_symbol_error(
            symbol_name,
            f"{symbol_name} is not available in the {section.value} section (restricted in: {', '.join(section_names)})")

    # Performance monitoring helpers (can be overridden by subclasses)
    def _log_cache_stats(self) -> None:
        """Log cache performance statistics for debugging."""
        if not self._dbg.debug:
            return

        stats = []
        for attr_name in ['_lookup_condition_cached', '_lookup_operator_cached', '_lookup_function_cached',
                          '_lookup_statement_function_cached']:
            if hasattr(self, attr_name):
                method = getattr(self, attr_name)
                if hasattr(method, 'cache_info'):
                    info = method.cache_info()
                    hit_rate = info.hits / (info.hits + info.misses) if (info.hits + info.misses) > 0 else 0.0
                    stats.append(f"{attr_name}: {info.hits}/{info.hits + info.misses} hits ({hit_rate:.1%})")

        if stats:
            self._dbg(f"Cache stats: {'; '.join(stats)}")

    # Cache management
    def clear_caches(self) -> None:
        """Clear all LRU caches to free memory or force refresh."""
        for attr_name in ['_lookup_condition_cached', '_lookup_operator_cached', '_lookup_function_cached',
                          '_lookup_statement_function_cached']:
            if hasattr(self, attr_name):
                method = getattr(self, attr_name)
                if hasattr(method, 'cache_clear'):
                    method.cache_clear()

        for attr_name in ['_condition_map', '_operator_map', '_function_map', '_statement_function_map', '_reverse_resolution_map']:
            if hasattr(self, attr_name):
                delattr(self, attr_name)

    # Context managers for debugging
    def debug_context(self, method_name: str, *args: Any):
        """Context manager for debug entry/exit."""

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
