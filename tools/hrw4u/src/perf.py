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
Essential performance utilities for HRW4U.

Provides core performance patterns for string building and caching.
"""

from __future__ import annotations

import io
import re
from typing import Final, Self


# Pre-compiled regex patterns - compile once at import time
class CompiledPatterns:
    """Centralized compiled regex patterns for performance."""

    # Use raw strings and compile at module level for optimal performance
    IDENTIFIER: Final = re.compile(r'^[a-zA-Z_][a-zA-Z0-9_]*$')
    HTTP_HEADER: Final = re.compile(r'^[@!#$%&\'*+\-.0-9A-Z^_`a-z|~]+$')
    PERCENT_BLOCK: Final = re.compile(r'^\%\{([A-Z0-9_:-]+)\}$')
    WHITESPACE: Final = re.compile(r'\s+')

    # Multi-line patterns
    COMMENT_BLOCK: Final = re.compile(r'/\*.*?\*/', re.DOTALL)
    STRING_INTERPOLATION: Final = re.compile(r'\{([a-zA-Z_][a-zA-Z0-9_.-]*(?:\([^)]*\))?)\}', re.MULTILINE)


class HighPerformanceStringBuilder:
    """
    High-performance string building using io.StringIO.

    Avoids quadratic string concatenation performance issues.
    """

    __slots__ = ('_buffer', '_size')

    def __init__(self) -> None:
        self._buffer = io.StringIO()
        self._size = 0

    def append(self, text: str) -> Self:
        """Append text to buffer."""
        self._buffer.write(text)
        self._size += len(text)
        return self

    def append_line(self, text: str) -> Self:
        """Append text with newline."""
        self._buffer.write(text)
        self._buffer.write('\n')
        self._size += len(text) + 1
        return self

    def join(self, items: list[str], separator: str = '') -> Self:
        """Join items with separator."""
        if items:
            self._buffer.write(separator.join(items))
            self._size += sum(len(item) for item in items) + len(separator) * (len(items) - 1)
        return self

    def build(self) -> str:
        """Get final string."""
        return self._buffer.getvalue()

    def clear(self) -> None:
        """Clear buffer for reuse."""
        self._buffer.seek(0)
        self._buffer.truncate(0)
        self._size = 0

    @property
    def size(self) -> int:
        """Get current buffer size."""
        return self._size


class StringBuilderMixin:
    """
    Mixin class providing efficient string building capabilities.

    Can be mixed into visitor classes for high-performance string operations.
    """

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._string_builder = HighPerformanceStringBuilder()

    def build_string(self, parts: list[str], separator: str = '\n') -> str:
        """Build string efficiently from parts."""
        self._string_builder.clear()
        self._string_builder.join(parts, separator)
        return self._string_builder.build()

    def append_to_builder(self, text: str) -> None:
        """Append text to internal builder."""
        self._string_builder.append(text)

    def get_built_string(self) -> str:
        """Get the current built string."""
        return self._string_builder.build()

    def clear_builder(self) -> None:
        """Clear the string builder."""
        self._string_builder.clear()


class OptimizedHotPaths:
    """
    Optimizations for frequently executed code paths.

    Demonstrates patterns for optimizing hot paths in compilers and LSP servers.
    """

    @staticmethod
    def fast_string_processing(text: str) -> str:
        """Optimized string processing avoiding repeated allocations."""
        # Use str.translate for character replacement (faster than multiple replace())
        translation_table = str.maketrans({'\t': ' ', '\r': '', '\n': ' '})
        return text.translate(translation_table)

    @staticmethod
    def fast_list_building(items: list[str]) -> list[str]:
        """Optimized list building using list comprehension."""
        # List comprehensions are faster than explicit loops
        return [item.strip().upper() for item in items if item.strip()]

    @staticmethod
    def fast_dict_lookup_with_default(lookup_dict: dict[str, str], keys: list[str]) -> list[str]:
        """Fast dictionary lookups with defaults."""
        # Use dict.get() to avoid KeyError overhead
        return [lookup_dict.get(key, "default") for key in keys]
