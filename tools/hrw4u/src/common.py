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

import re
from enum import Enum
from typing import Set, Final, Protocol
from functools import cached_property
from hrw4u.states import SectionType
from hrw4u.errors import SymbolResolutionError


class RegexPatterns:
    """Compiled regex patterns for reuse across modules"""
    SIMPLE_TOKEN: Final = re.compile(r'^[@a-zA-Z0-9_-]+$')
    HTTP_TOKEN: Final = re.compile(r'^[@!#$%&\'*+\-.0-9A-Z^_`a-z|~]+$')
    REGEX_LITERAL: Final = re.compile(r'^/(?:\\.|[^/\r\n])+/$')
    PERCENT_BLOCK: Final = re.compile(r"^\%\{([A-Z0-9_-]+)(?::(.*))?\}$")
    PERCENT_INLINE: Final = re.compile(r"%\{([A-Z0-9_-]+)(?::(.*?))?\}")
    PERCENT_PATTERN: Final = re.compile(r'%\{([^}]+)\}')
    SUBSTITUTE_PATTERN: Final = re.compile(
        r"""(?<!%)\{\s*(?P<func>[a-zA-Z_][a-zA-Z0-9_-]*)\s*\((?P<args>[^)]*)\)\s*\}
            |
            (?<!%)\{(?P<var>[^{}()]+)\}
        """,
        re.VERBOSE,
    )


class VisitorMixin:
    """Common visitor functionality"""
    INDENT_SPACES: Final = 4

    def format_with_indent(self, text: str, indent_level: int) -> str:
        """Format text with proper indentation"""
        return " " * (indent_level * self.INDENT_SPACES) + text


class SectionValidator:
    """Common section validation logic"""

    @staticmethod
    def validate_section_access(name: str, section: SectionType | None, restricted: Set[SectionType] | None) -> None:
        """Validate if a symbol can be used in the given section"""
        if section and restricted and section in restricted:
            raise SymbolResolutionError(name, f"{name} is not available in the {section.value} section")


class MagicStrings(str, Enum):
    """Common magic strings used throughout the codebase"""
    # Header operations
    RM_HEADER = "rm-header"
    SET_HEADER = "set-header"
    RM_COOKIE = "rm-cookie"
    SET_COOKIE = "set-cookie"
    RM_DESTINATION = "rm-destination"
    SET_DESTINATION = "set-destination"

    # Status operations
    SET_STATUS = "set-status"
    SET_STATUS_REASON = "set-status-reason"

    # Body operations
    SET_BODY = "set-body"
    SET_BODY_FROM = "set-body-from"

    # Control operations
    NO_OP = "no-op"
    SET_DEBUG = "set-debug"
    SET_CONFIG = "set-config"
    SET_REDIRECT = "set-redirect"
    SKIP_REMAP = "skip-remap"
    RUN_PLUGIN = "run-plugin"
    COUNTER = "counter"

    # Connection operations
    SET_CONN_DSCP = "set-conn-dscp"
    SET_HTTP_CNTL = "set-http-cntl"

    # Query operations
    REMOVE_QUERY = "remove_query"
    KEEP_QUERY = "keep_query"


class SystemDefaults:
    """System-wide default constants"""
    DEFAULT_FILENAME: Final = "<stdin>"
    DEFAULT_DEBUG: Final = False
    DEFAULT_CONFIGURABLE: Final = False
    LINE_NUMBER_WIDTH: Final = 4


class HeaderOperations:
    """Header operation constants as tuple for backward compatibility"""
    OPERATIONS: Final = (MagicStrings.RM_HEADER.value, MagicStrings.SET_HEADER.value)


# Protocol classes for better type safety
class ValidatorProtocol(Protocol):
    """Protocol for validation functions"""

    def __call__(self, value: str) -> None:
        ...


class ResolverProtocol(Protocol):
    """Protocol for symbol resolvers"""

    def resolve_condition(self, name: str, section: SectionType | None = None) -> tuple[str, bool]:
        ...

    def resolve_assignment(self, name: str, value: str, section: SectionType | None = None) -> str:
        ...


class InverseResolverProtocol(Protocol):
    """Protocol for inverse symbol resolvers"""

    def percent_to_ident_or_func(self, percent: str, section: SectionType | None) -> tuple[str, bool]:
        ...

    def op_to_hrw4u(self, cmd: str, args: list[str], section: SectionType | None, op_state) -> str:
        ...
