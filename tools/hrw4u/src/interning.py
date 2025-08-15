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
String interning utilities for memory optimization.

This module provides centralized string interning using Python's sys.intern()
for frequently used strings to reduce memory footprint and improve comparison performance.
"""

from __future__ import annotations

import sys
from typing import Final


class StringInterning:
    """Centralized string interning for frequently used strings."""

    # Language keywords and literals
    # Import here to avoid circular dependency
    from hrw4u.types import LanguageKeyword
    KEYWORDS: Final[dict[str, str]] = {kw.keyword: sys.intern(kw.keyword) for kw in LanguageKeyword}

    # Section names - interned for fast comparisons
    SECTIONS: Final[dict[str, str]] = {
        section: sys.intern(section) for section in
        ['REMAP', 'SEND_REQUEST', 'READ_RESPONSE', 'SEND_RESPONSE', 'READ_REQUEST', 'PRE_REMAP', 'TXN_START', 'TXN_CLOSE', 'VARS']
    }

    # Hook names - used frequently in mapping operations
    HOOKS: Final[dict[str, str]] = {
        hook: sys.intern(hook) for hook in [
            'REMAP_PSEUDO_HOOK', 'SEND_REQUEST_HDR_HOOK', 'READ_RESPONSE_HDR_HOOK', 'SEND_RESPONSE_HDR_HOOK',
            'READ_REQUEST_HDR_HOOK', 'READ_REQUEST_PRE_REMAP_HOOK', 'TXN_START_HOOK', 'TXN_CLOSE_HOOK', 'VARS_SECTION'
        ]
    }

    # Condition and operator modifiers
    MODIFIERS: Final[dict[str, str]] = {
        mod: sys.intern(mod) for mod in ['AND', 'OR', 'NOT', 'NOCASE', 'PRE', 'SUF', 'EXT', 'MID', 'I', 'L', 'QSA']
    }
    # NOTE: Operator/condition prefixes are NOT interned because they are used
    # as static dict keys, not in hot comparison paths
    # LSP-related strings
    LSP_STRINGS: Final[dict[str, str]] = {
        string: sys.intern(string) for string in [
            'markdown', 'kind', 'value', 'label', 'detail', 'documentation', 'insertText', 'textEdit', 'insertTextFormat', 'range',
            'newText'
        ]
    }

    @classmethod
    def intern_keyword(cls, keyword: str) -> str:
        """Intern a language keyword, returning the interned version if available."""
        return cls.KEYWORDS.get(keyword, sys.intern(keyword))

    @classmethod
    def intern_section(cls, section: str) -> str:
        """Intern a section name, returning the interned version if available."""
        return cls.SECTIONS.get(section, sys.intern(section))

    @classmethod
    def intern_hook(cls, hook: str) -> str:
        """Intern a hook name, returning the interned version if available."""
        return cls.HOOKS.get(hook, sys.intern(hook))

    @classmethod
    def intern_modifier(cls, modifier: str) -> str:
        """Intern a modifier string, returning the interned version if available."""
        return cls.MODIFIERS.get(modifier.upper(), sys.intern(modifier.upper()))

    @classmethod
    def intern_lsp_string(cls, string: str) -> str:
        """Intern an LSP-related string, returning the interned version if available."""
        return cls.LSP_STRINGS.get(string, sys.intern(string))

    @classmethod
    def intern_any(cls, string: str) -> str:
        """General-purpose string interning with fallback to sys.intern()."""
        return sys.intern(string)


# Convenience functions for common interning operations
def intern_keyword(keyword: str) -> str:
    """Convenience function to intern language keywords."""
    return StringInterning.intern_keyword(keyword)


def intern_section(section: str) -> str:
    """Convenience function to intern section names."""
    return StringInterning.intern_section(section)


def intern_hook(hook: str) -> str:
    """Convenience function to intern hook names."""
    return StringInterning.intern_hook(hook)


def intern_modifier(modifier: str) -> str:
    """Convenience function to intern modifier strings."""
    return StringInterning.intern_modifier(modifier)


def intern_operator_prefix(prefix: str) -> str:
    """Convenience function to intern operator prefixes."""
    return StringInterning.intern_operator_prefix(prefix)


def intern_condition_prefix(prefix: str) -> str:
    """Convenience function to intern condition prefixes."""
    return StringInterning.intern_condition_prefix(prefix)


def intern_lsp_string(string: str) -> str:
    """Convenience function to intern LSP-related strings."""
    return StringInterning.intern_lsp_string(string)


def intern_any(string: str) -> str:
    """General-purpose string interning."""
    return StringInterning.intern_any(string)
