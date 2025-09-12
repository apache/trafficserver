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
Programmatic table generation utilities to eliminate duplication.

This module provides utilities to generate reverse mappings and derived tables
from the primary forward mapping tables, ensuring single source of truth and
reducing maintenance overhead.
"""

from __future__ import annotations

from typing import Any
from functools import cache
from hrw4u.states import SectionType


class TableGenerator:
    """Generates derived tables and reverse mappings from primary tables."""

    def __init__(self) -> None:
        pass

    @staticmethod
    def _clean_tag(tag: str) -> str:
        """Extract clean tag name from %{TAG:payload} format."""
        return tag.strip().removeprefix('%{').removesuffix('}').split(':')[0]

    def generate_reverse_condition_map(self, condition_map: tuple[tuple[str, tuple], ...]) -> dict[str, str]:
        """Generate reverse condition mapping from forward condition map."""
        reverse_map = {}

        for ident_key, (tag, _, _, _, _, _) in condition_map:
            if not ident_key.endswith('.'):
                clean_tag = self._clean_tag(tag)
                reverse_map[clean_tag] = ident_key

        return reverse_map

    def generate_reverse_function_map(self, function_map: tuple[tuple[str, tuple], ...]) -> dict[str, str]:
        """Generate reverse function mapping from forward function map."""
        return {tag: func_name for func_name, (tag, _) in function_map}

    @cache
    def generate_section_hook_mapping(self) -> dict[str, str]:
        """Generate section name to hook name mapping."""
        return {section.value: section.hook_name for section in SectionType}

    @cache
    def generate_hook_section_mapping(self) -> dict[str, str]:
        """Generate hook name to section name mapping."""
        return {section.hook_name: section.value for section in SectionType}

    def generate_ip_mapping(self) -> dict[str, str]:
        """Generate IP payload to identifier mapping from CONDITION_MAP."""
        from hrw4u.tables import CONDITION_MAP

        ip_mapping = {}
        for condition_key, (tag, *_, reverse_info) in CONDITION_MAP.items():
            if reverse_info and reverse_info.get("reverse_tag") == "IP":
                payload = reverse_info.get("reverse_payload")
                if payload:
                    ip_mapping[payload] = condition_key
        return ip_mapping

    def generate_status_target_mapping(self) -> dict[frozenset[SectionType], str]:
        """Generate status target mappings based on section restrictions."""
        return {
            frozenset({SectionType.REMAP, SectionType.SEND_RESPONSE}): "inbound.status",
            frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST,
                       SectionType.READ_RESPONSE}): "outbound.status"
        }

    def generate_context_mappings(self) -> dict[str, dict[SectionType | frozenset[SectionType], str]]:
        """Generate context type mappings for headers, URLs, etc."""
        return {
            "HEADER_CONTEXT_MAP":
                {
                    SectionType.REMAP: "inbound.req.",
                    frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}): "outbound.req.",
                    SectionType.READ_RESPONSE: "outbound.resp."
                },
            "URL_CONTEXT_MAP":
                {
                    SectionType.REMAP: "inbound.url.",
                    frozenset({SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST}): "outbound.url."
                }
        }

    def generate_ambiguous_tag_resolution(self) -> dict[str, dict[str, Any]]:
        """Generate ambiguous tag resolution mappings."""
        return {
            "STATUS":
                {
                    "outbound_sections":
                        frozenset(
                            {SectionType.PRE_REMAP, SectionType.READ_REQUEST, SectionType.SEND_REQUEST, SectionType.READ_RESPONSE}),
                    "outbound_result": "outbound.status",
                    "inbound_result": "inbound.status"
                },
            "METHOD":
                {
                    "outbound_sections": frozenset({SectionType.SEND_REQUEST}),
                    "outbound_result": "outbound.method",
                    "inbound_result": "inbound.method"
                }
        }

    def generate_complete_reverse_resolution_map(self) -> dict[str, Any]:
        """Generate the complete reverse resolution map.

        Combines all individual mapping generators into a single structure.
        """
        reverse_map = {}

        # Add IP mappings
        reverse_map["IP"] = self.generate_ip_mapping()

        # Add ambiguous tag resolution
        ambiguous_mappings = self.generate_ambiguous_tag_resolution()
        for tag, mapping in ambiguous_mappings.items():
            reverse_map[tag] = mapping

        # Add status target mappings
        reverse_map["STATUS_TARGETS"] = self.generate_status_target_mapping()

        # Add context mappings
        context_mappings = self.generate_context_mappings()
        for name, mapping in context_mappings.items():
            reverse_map[name] = mapping

        # Add context type mappings
        from hrw4u.tables import CONTEXT_TYPE_MAP, FALLBACK_TAG_MAP
        reverse_map["CONTEXT_TYPE_MAP"] = CONTEXT_TYPE_MAP.copy()

        # Add fallback tag mappings
        reverse_map["FALLBACK_TAG_MAP"] = FALLBACK_TAG_MAP.copy()

        return reverse_map


# Singleton instance for global use
_table_generator = TableGenerator()


def get_reverse_condition_map(condition_map: dict[str, tuple]) -> dict[str, str]:
    """Get reverse condition mapping."""
    return _table_generator.generate_reverse_condition_map(tuple(condition_map.items()))


def get_reverse_function_map(function_map: dict[str, tuple]) -> dict[str, str]:
    """Get reverse function mapping."""
    return _table_generator.generate_reverse_function_map(tuple(function_map.items()))


def get_section_mappings() -> tuple[dict[str, str], dict[str, str]]:
    """Get both section->hook and hook->section mappings."""
    return (_table_generator.generate_section_hook_mapping(), _table_generator.generate_hook_section_mapping())


def get_complete_reverse_resolution_map() -> dict[str, Any]:
    """Get the complete generated reverse resolution map."""
    return _table_generator.generate_complete_reverse_resolution_map()
