#!/usr/bin/env python3
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
"""Centralized string handling for the HRW4U LSP server."""

from __future__ import annotations

import re
from typing import Any, Dict

from .documentation import LSP_STRING_LITERAL_INFO
from .hover import HoverInfoProvider, InterpolationHoverProvider
from .types import (CompletionContext, LSPPosition, VariableDeclaration, DiagnosticRange, LSPDiagnostic)


class StringLiteralHandler:
    """Handles string literal processing and hover information."""

    @staticmethod
    def _create_string_literal_hover() -> Dict[str, Any]:
        """Create standardized string literal hover info."""
        return HoverInfoProvider.create_hover_info(
            f"**{LSP_STRING_LITERAL_INFO['name']}** - HRW4U String Literal\n\n{LSP_STRING_LITERAL_INFO['description']}")

    @staticmethod
    def check_string_literal(line: str, character: int) -> Dict[str, Any] | None:
        """Check if the cursor is inside a string literal and parse interpolated expressions."""
        in_single_quote = False
        in_double_quote = False
        quote_start = -1

        for i, char in enumerate(line):
            if char == '"' and not in_single_quote:
                if not in_double_quote:
                    in_double_quote = True
                    quote_start = i
                else:
                    if quote_start <= character <= i:
                        string_content = line[quote_start + 1:i]
                        cursor_in_string = character - quote_start - 1

                        interpolation_info = InterpolationHandler.check_interpolated_expression(string_content, cursor_in_string)
                        if interpolation_info:
                            return interpolation_info

                        return StringLiteralHandler._create_string_literal_hover()
                    in_double_quote = False

            elif char == "'" and not in_double_quote:
                if not in_single_quote:
                    in_single_quote = True
                    quote_start = i
                else:
                    if quote_start <= character <= i:
                        string_content = line[quote_start + 1:i]
                        cursor_in_string = character - quote_start - 1

                        interpolation_info = InterpolationHandler.check_interpolated_expression(string_content, cursor_in_string)
                        if interpolation_info:
                            return interpolation_info

                        return StringLiteralHandler._create_string_literal_hover()
                    in_single_quote = False

        return None


class InterpolationHandler:
    """Handles string interpolation expression processing."""

    @staticmethod
    def check_interpolated_expression(string_content: str, cursor_pos: int) -> Dict[str, Any] | None:
        """Check if the cursor is over an interpolated expression like {geo.country}."""
        # Find all interpolated expressions {expression}
        for match in re.finditer(r'\{([^}]+)\}', string_content):
            start, end = match.span()
            if start <= cursor_pos <= end:
                expression = match.group(1).strip()

                # Try to get hover info for this expression
                hover_info = InterpolationHoverProvider.get_interpolated_expression_info(expression)
                if hover_info:
                    return hover_info

                # Fallback for unrecognized interpolations
                return HoverInfoProvider.create_hover_info(
                    f"**{{{expression}}}** - HRW4U String Interpolation\n\nVariable substitution in string literals.")

        return None


class ContextAnalyzer:
    """Analyzes completion context for the LSP server."""

    @staticmethod
    def determine_completion_context(lines: list[str], line: int, character: int) -> CompletionContext:
        """Determine the completion context based on cursor position."""
        current_line = lines[line]
        prefix = current_line[:character]

        context: CompletionContext = {
            "is_section_context": False,
            "has_dot": False,
            "dot_prefix": "",
            "is_function_context": False,
            "allows_keywords": False,
            "current_section": None,
            "replacement_range": None
        }

        stripped_prefix = prefix.strip()
        if not stripped_prefix or (stripped_prefix and not any(c in stripped_prefix for c in ['.', '=', '(', ')'])):
            rest_of_line = current_line[character:].strip()
            if not rest_of_line or rest_of_line.startswith('{'):
                context["is_section_context"] = True

        if "." in prefix:
            parts = prefix.split()
            if parts:
                last_part = parts[-1]
                if "." in last_part:
                    context["has_dot"] = True
                    context["dot_prefix"] = last_part.split(".")[0] + "."

                    word_start = len(prefix) - len(last_part)
                    context["replacement_range"] = {
                        "start": {
                            "line": line,
                            "character": word_start
                        },
                        "end": {
                            "line": line,
                            "character": character
                        }
                    }

        if not context["has_dot"] and not context["is_section_context"]:
            context["is_function_context"] = True
            context["allows_keywords"] = True

        # Determine current section
        from hrw4u.states import SectionType
        for i in range(line, -1, -1):
            line_text = lines[i].strip()
            if '{' in line_text and not line_text.startswith('{'):
                section_name = line_text.split('{')[0].strip()
                try:
                    context["current_section"] = SectionType(section_name)
                    break
                except ValueError:
                    pass

        return context


class ExpressionParser:
    """Parses various types of expressions for hover information."""

    @staticmethod
    def parse_dotted_expression(line: str, character: int) -> Dict[str, Any] | None:
        """Parse dotted expressions like outbound.req.X-Fie or inbound.req.@X-foo."""
        from .hover import DottedExpressionHoverProvider

        expr_start = character
        expr_end = character

        while expr_start > 0 and (line[expr_start - 1].isalnum() or line[expr_start - 1] in '._@-'):
            expr_start -= 1

        while expr_end < len(line) and (line[expr_end].isalnum() or line[expr_end] in '._@-'):
            expr_end += 1

        if expr_start == expr_end:
            return None

        full_expression = line[expr_start:expr_end]

        return DottedExpressionHoverProvider.parse_dotted_expression(full_expression, character, expr_start)


class DocumentAnalyzer:
    """Analyzes document content for various purposes."""

    @staticmethod
    def parse_variable_declarations(text: str) -> Dict[str, VariableDeclaration]:
        """Parse variable declarations from VARS sections."""
        from hrw4u.types import VarType

        variable_declarations = {}
        lines = text.split('\n')
        in_vars_section = False
        brace_count = 0

        for line_num, line in enumerate(lines):
            stripped = line.strip()

            # Skip comments and empty lines
            if not stripped or stripped.startswith('//') or stripped.startswith('#'):
                continue

            # Check for VARS section start
            if stripped == 'VARS {' or stripped.startswith('VARS {'):
                in_vars_section = True
                brace_count = 1
                continue

            if in_vars_section:
                brace_count += stripped.count('{') - stripped.count('}')

                if brace_count <= 0:
                    in_vars_section = False
                    continue

                if ':' in stripped and ';' in stripped:
                    try:
                        decl = stripped.rstrip(';').strip()
                        if ':' in decl:
                            var_name, var_type = decl.split(':', 1)
                            var_name = var_name.strip()
                            var_type = var_type.strip()

                            try:
                                vt = VarType.from_str(var_type.lower())
                                description = vt.description
                            except ValueError:
                                description = f"Variable of type {var_type}"

                            variable_declarations[var_name] = VariableDeclaration(
                                type=var_type, description=description, line=line_num)
                    except Exception:
                        continue

        return variable_declarations

    @staticmethod
    def validate_section_names(text: str) -> list[LSPDiagnostic]:
        """Pre-validate section names in the document with optimization."""
        from hrw4u.states import SectionType
        from functools import lru_cache

        @lru_cache(maxsize=128)
        def cached_section_validation(section_name: str) -> bool:
            """Cache section name validation."""
            valid_sections = {section.value for section in SectionType}
            return section_name in valid_sections

        diagnostics = []
        lines = text.split('\n')

        # Pre-compute valid sections once
        valid_sections_list = sorted([section.value for section in SectionType])
        valid_sections_str = ', '.join(valid_sections_list)

        for line_num, line in enumerate(lines):
            stripped = line.strip()
            if not stripped or stripped.startswith(('//', '#')):
                continue

            # Look for section declarations (IDENT followed by {)
            if '{' in stripped and not stripped.startswith('{'):
                parts = stripped.split('{', 1)
                potential_section = parts[0].strip()

                # Skip if contains spaces or '=' (not a valid section declaration)
                if ' ' in potential_section or '=' in potential_section:
                    continue
                if potential_section.startswith('}'):
                    continue

                # Skip if it's a known language keyword
                from hrw4u.types import LanguageKeyword

                language_keywords = {kw.keyword for kw in LanguageKeyword}
                if potential_section.lower() in language_keywords:
                    continue

                # Use cached validation
                if potential_section and not cached_section_validation(potential_section):
                    start_pos = line.find(potential_section)
                    end_pos = start_pos + len(potential_section)

                    diagnostics.append(
                        LSPDiagnostic(
                            range=DiagnosticRange(
                                start=LSPPosition(line=line_num, character=start_pos),
                                end=LSPPosition(line=line_num, character=end_pos)),
                            severity=1,
                            message=f"Invalid section name: '{potential_section}'. Valid sections are: {valid_sections_str}",
                            source="hrw4u-section-validator"))

        return diagnostics
