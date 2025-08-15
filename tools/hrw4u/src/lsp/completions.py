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
Centralized completion helpers for LSP functionality.
This module provides utilities for creating completion items.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Any
from hrw4u.states import SectionType
from hrw4u.types import VarType, LanguageKeyword
import hrw4u.tables as tables
from hrw4u.interning import intern_keyword, intern_lsp_string
from . import documentation as doc


@dataclass(slots=True, frozen=True)
class CompletionItem:
    """Represents a completion item with all necessary LSP fields."""
    label: str
    kind: int
    detail: str
    documentation: dict[str, str]
    insert_text: str | None = None
    text_edit: dict[str, Any] | None = None
    insert_text_format: int = 1

    def to_lsp_dict(self) -> dict[str, Any]:
        """Convert to LSP completion item dictionary."""
        item = {
            intern_lsp_string("label"): self.label,
            intern_lsp_string("kind"): self.kind,
            intern_lsp_string("detail"): self.detail,
            intern_lsp_string("documentation"): self.documentation
        }

        if self.insert_text:
            item[intern_lsp_string("insertText")] = self.insert_text
        if self.text_edit:
            item[intern_lsp_string("textEdit")] = self.text_edit
        if self.insert_text_format != 1:
            item[intern_lsp_string("insertTextFormat")] = self.insert_text_format

        return item


class LSPCompletionItemKind(IntEnum):
    """LSP Completion Item Kinds - official LSP specification values."""
    TEXT = 1
    METHOD = 2
    FUNCTION = 3
    CONSTRUCTOR = 4
    FIELD = 5
    VARIABLE = 6
    CLASS = 7
    INTERFACE = 8
    MODULE = 9
    PROPERTY = 10
    UNIT = 11
    VALUE = 12
    ENUM = 13
    KEYWORD = 14
    SNIPPET = 15
    COLOR = 16
    FILE = 17
    REFERENCE = 18
    FOLDER = 19
    ENUM_MEMBER = 20
    CONSTANT = 21
    STRUCT = 22
    EVENT = 23
    OPERATOR = 24
    TYPE_PARAMETER = 25


class CompletionBuilder:
    """Builder for creating various types of completion items."""

    @staticmethod
    def create_markdown_doc(value: str) -> dict[str, str]:
        """Create markdown documentation object."""
        return {intern_lsp_string("kind"): intern_lsp_string("markdown"), intern_lsp_string("value"): value}

    @classmethod
    def operator_completion(
            cls, label: str, commands: str | list[str] | tuple[str, ...], sections: set[SectionType] | None,
            current_section: SectionType | None, replacement_range: dict[str, Any]) -> CompletionItem | None:
        """Create completion item for operators."""
        if sections and current_section and current_section in sections:
            return None

        cmd_str = commands if isinstance(commands, str) else " / ".join(commands)
        detail = f"Operation: {cmd_str}"

        if sections:
            section_names = [s.value for s in sections]
            detail += f" (Restricted in: {', '.join(section_names)})"

        documentation = cls.create_markdown_doc(f"**{label}** - HRW4U Operator\n\nMaps to: `{cmd_str}`")

        return CompletionItem(
            label=label,
            kind=LSPCompletionItemKind.FIELD,
            detail=detail,
            documentation=documentation,
            text_edit={
                intern_lsp_string("range"): replacement_range,
                intern_lsp_string("newText"): label
            })

    @classmethod
    def condition_completion(
            cls, label: str, tag: str, sections: set[SectionType] | None, current_section: SectionType | None,
            replacement_range: dict[str, Any]) -> CompletionItem | None:
        """Create completion item for conditions."""
        if sections and current_section and current_section in sections:
            return None

        detail = f"Condition: {tag}"

        if sections:
            section_names = [s.value for s in sections]
            detail += f" (Restricted in: {', '.join(section_names)})"

        documentation = cls.create_markdown_doc(f"**{label}** - HRW4U Condition\n\nMaps to: `{tag}`")

        return CompletionItem(
            label=label,
            kind=LSPCompletionItemKind.FIELD,
            detail=detail,
            documentation=documentation,
            text_edit={
                intern_lsp_string("range"): replacement_range,
                intern_lsp_string("newText"): label
            })

    @classmethod
    def function_completion(cls, func_name: str, tag: str, function_type: str) -> CompletionItem:
        """Create completion item for functions."""
        detail = f"{function_type}: {tag}"
        documentation_value = (
            f"**{func_name}()** - HRW4U {function_type} Function\n\n"
            f"Maps to: `{tag}`\n\n"
            f"Used {'in conditional expressions' if function_type == 'Function' else 'as statements in code blocks'}.")

        # Use comprehensive documentation if available
        if func_name in doc.LSP_FUNCTION_DOCUMENTATION:
            func_doc = doc.LSP_FUNCTION_DOCUMENTATION[func_name]
            detail = f"{func_doc.category}: {func_doc.name}"
            documentation_value = (f"**{func_doc.name}**\n\n"
                                   f"{func_doc.description}\n\n"
                                   f"**Syntax:** `{func_doc.syntax}`")

        documentation = cls.create_markdown_doc(documentation_value)

        return CompletionItem(
            label=func_name,
            kind=LSPCompletionItemKind.FUNCTION,
            detail=detail,
            documentation=documentation,
            insert_text=f"{func_name}($0)",
            insert_text_format=2)

    @classmethod
    def keyword_completion(cls, keyword: str, desc: str) -> CompletionItem:
        """Create completion item for keywords."""
        documentation = cls.create_markdown_doc(f"**{keyword}** - HRW4U keyword")
        return CompletionItem(label=keyword, kind=LSPCompletionItemKind.KEYWORD, detail=desc, documentation=documentation)

    @classmethod
    def section_completion(cls, section: SectionType) -> CompletionItem:
        """Create completion item for sections."""
        documentation = cls.create_markdown_doc(
            f"**{section.value}** - HRW4U Section\n\n"
            f"Maps to ATS hook: `{section.hook_name}`")

        return CompletionItem(
            label=section.value,
            kind=LSPCompletionItemKind.KEYWORD,
            detail=f"Hook: {section.hook_name}",
            documentation=documentation,
            insert_text=f"{section.value} {{\n\t$0\n}}",
            insert_text_format=2  # Snippet format
        )

    @classmethod
    def variable_type_completion(cls, var_type: str) -> CompletionItem:
        """Create completion item for variable types."""
        documentation = cls.create_markdown_doc(
            f"**{var_type}** - HRW4U Variable Type\n\n"
            f"Used for declaring variables in the VARS section.")

        return CompletionItem(
            label=var_type,
            kind=LSPCompletionItemKind.TYPE_PARAMETER,
            detail=f"Variable type: {var_type}",
            documentation=documentation)


class CompletionProvider:
    """High-level completion provider that orchestrates completion generation."""

    def __init__(self) -> None:
        self.builder = CompletionBuilder()

    def get_operator_completions(self, base_prefix: str, current_section: SectionType | None,
                                 replacement_range: dict[str, Any]) -> list[dict[str, Any]]:
        """Get completions for operators and conditions with the given prefix."""
        completions = []
        seen_labels = set()

        # Add condition completions
        for key, (tag, _, _, sections, _, _) in tables.CONDITION_MAP.items():
            if key.startswith(base_prefix) and key not in seen_labels:
                seen_labels.add(key)

                item = self.builder.condition_completion(key, tag, sections, current_section, replacement_range)
                if item:
                    completions.append(item.to_lsp_dict())

        # Add operator completions
        for key, (commands, _, _, sections) in tables.OPERATOR_MAP.items():
            if key.startswith(base_prefix) and key not in seen_labels:
                seen_labels.add(key)

                item = self.builder.operator_completion(key, commands, sections, current_section, replacement_range)
                if item:
                    completions.append(item.to_lsp_dict())

        return completions

    def get_function_completions(self) -> list[dict[str, Any]]:
        """Get completions for all functions."""
        completions = []

        # Regular functions
        for func_name, (tag, _) in tables.FUNCTION_MAP.items():
            item = self.builder.function_completion(func_name, tag, "Function")
            completions.append(item.to_lsp_dict())

        # Statement functions
        for func_name, (tag, _) in tables.STATEMENT_FUNCTION_MAP.items():
            item = self.builder.function_completion(func_name, tag, "Statement")
            completions.append(item.to_lsp_dict())

        return completions

    def get_keyword_completions(self) -> list[dict[str, Any]]:
        """Get completions for keywords."""
        completions = []
        for lang_keyword in LanguageKeyword:
            # Use interned version of the keyword
            interned_keyword = intern_keyword(lang_keyword.keyword)
            item = self.builder.keyword_completion(interned_keyword, lang_keyword.description)
            completions.append(item.to_lsp_dict())
        return completions

    def get_section_completions(self) -> list[dict[str, Any]]:
        """Get completions for sections."""
        completions = []
        for section in SectionType:
            item = self.builder.section_completion(section)
            completions.append(item.to_lsp_dict())
        return completions

    def get_variable_type_completions(self) -> list[dict[str, Any]]:
        """Get completions for variable types."""
        completions = []
        for var_type_enum in VarType:
            var_type = var_type_enum.value[0]
            item = self.builder.variable_type_completion(var_type)
            completions.append(item.to_lsp_dict())
        return completions
