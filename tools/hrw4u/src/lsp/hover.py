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
"""Centralized hover information providers for the HRW4U LSP server."""

from __future__ import annotations

import re
from typing import Any, Dict

from . import documentation as doc
from hrw4u.tables import OPERATOR_MAP, CONDITION_MAP, FUNCTION_MAP, STATEMENT_FUNCTION_MAP, LSPPatternMatcher
from hrw4u.states import SectionType
from hrw4u.types import VarType


class HoverInfoProvider:
    """Centralized provider for hover information generation."""

    @staticmethod
    def create_hover_info(markdown_content: str) -> Dict[str, Any]:
        """Create a standardized hover info dictionary."""
        return {"contents": {"kind": "markdown", "value": markdown_content}}

    @staticmethod
    def create_field_interpolation_hover(
            expression: str, field_display: str, field_desc: str, context: str, maps_to: str, usage: str = None) -> Dict[str, Any]:
        """Create hover info for field interpolations with a standard format."""
        usage_text = usage or "Used in string value interpolation."

        description = (
            f"**{{{expression}}}** - {field_display} Interpolation\n\n"
            f"**Context:** {context}\n\n"
            f"**Description:** {field_desc}\n\n"
            f"**Maps to:** `{maps_to}`\n\n"
            f"{usage_text}")

        return HoverInfoProvider.create_hover_info(description)

    @staticmethod
    def create_field_hover(expression: str,
                           field_display: str,
                           field_desc: str,
                           context: str,
                           maps_to: str,
                           usage: str = None) -> Dict[str, Any]:
        """Create hover info for field expressions with a standard format."""
        usage_text = usage or "Used in expression evaluation."

        description = (
            f"**{expression}** - {field_display}\n\n"
            f"**Context:** {context}\n\n"
            f"**Description:** {field_desc}\n\n"
            f"**Maps to:** `{maps_to}`\n\n"
            f"{usage_text}")

        return HoverInfoProvider.create_hover_info(description)


class CertificateHoverProvider:
    """Specialized hover provider for certificate expressions."""

    @staticmethod
    def parse_certificate_expression(expression: str, is_interpolation: bool = False) -> Dict[str, Any] | None:
        """Parse certificate expressions using table-driven approach."""
        parsed_data = doc.CertificatePattern.parse_certificate_expression(expression, is_interpolation)

        if parsed_data:
            hover_text = doc.CertificatePattern.create_certificate_hover(expression, parsed_data, is_interpolation)
            return HoverInfoProvider.create_hover_info(hover_text)

        return None


class InterpolationHoverProvider:
    """Specialized hover provider for string interpolation expressions."""

    @staticmethod
    def get_interpolated_expression_info(expression: str) -> Dict[str, Any] | None:
        """Get hover info for interpolated expressions."""
        # Try table-driven pattern matching first
        if match := LSPPatternMatcher.match_any_pattern(expression):
            return InterpolationHoverProvider._handle_pattern_match(match, expression, is_interpolation=True)

        # Handle IP patterns using centralized patterns (exact matches)
        if expression in doc.IP_PATTERNS:
            ip_info = doc.IP_PATTERNS[expression]
            return HoverInfoProvider.create_field_interpolation_hover(
                expression, ip_info.name, ip_info.description, "IP Address Field", ip_info.maps_to,
                "Used for IP-based routing and logging in string values.")

        # Handle URL patterns using table-driven approach
        parsed_url = doc.URLPattern.parse_url_expression(expression, is_interpolation=True)
        if parsed_url:
            hover_text = doc.URLPattern.create_url_hover(parsed_url, is_interpolation=True)
            return HoverInfoProvider.create_hover_info(hover_text)

        # Handle capture groups using table-driven approach
        parsed_capture = doc.CapturePattern.parse_capture_expression(expression, is_interpolation=True)
        if parsed_capture:
            hover_text = doc.CapturePattern.create_capture_hover(parsed_capture, is_interpolation=True)
            return HoverInfoProvider.create_hover_info(hover_text)

        return None

    @staticmethod
    def _handle_pattern_match(match, expression: str, is_interpolation: bool = False) -> Dict[str, Any] | None:
        """Handle a matched pattern and generate appropriate hover info."""
        if match.context_type == 'Certificate':
            return CertificateHoverProvider.parse_certificate_expression(expression, is_interpolation=is_interpolation)

        if match.field_dict_key and match.suffix:
            # Handle field patterns (now., id., geo.)
            field_dict = getattr(doc, match.field_dict_key)
            suffix_key = match.suffix.upper()
            if suffix_key in field_dict:
                field_info = field_dict[suffix_key]
                return HoverInfoProvider.create_field_interpolation_hover(
                    expression, field_info.name, field_info.description, match.context_type, match.maps_to or
                    f"%{{{match.context_type.upper()}:{suffix_key}}}",
                    f"Used for {match.context_type.lower()} information in string values.")

        return None


class DottedExpressionHoverProvider:
    """Specialized hover provider for dotted expressions like outbound.req.X-Field."""

    @staticmethod
    def parse_dotted_expression(full_expression: str, character_pos: int, expr_start: int) -> Dict[str, Any] | None:
        """Parse dotted expressions and provide appropriate hover info."""
        cursor_pos = character_pos - expr_start

        # Try table-driven pattern matching first
        if match := LSPPatternMatcher.match_any_pattern(full_expression):
            return DottedExpressionHoverProvider._handle_pattern_match(match, full_expression, cursor_pos, expr_start)

        # Handle URL patterns using table-driven approach
        parsed_url = doc.URLPattern.parse_url_expression(full_expression, is_interpolation=False)
        if parsed_url:
            cursor_pos = character_pos - expr_start

            prefix_url_len = len(f"{parsed_url['prefix']}.url.")
            if cursor_pos < prefix_url_len:
                context = f"{parsed_url['prefix']}.url"
                return OperatorHoverProvider.get_operator_hover_info(context)
            else:
                hover_text = doc.URLPattern.create_url_hover(parsed_url, is_interpolation=False)
                return HoverInfoProvider.create_hover_info(hover_text)

        # Handle exact IP matches using centralized patterns
        if full_expression in doc.IP_PATTERNS:
            ip_info = doc.IP_PATTERNS[full_expression]
            description = (
                f"**{full_expression}** - {ip_info.name}\n\n"
                f"**Context:** {ip_info.description}\n\n"
                f"**Maps to:** `{ip_info.maps_to}`\n\n"
                f"{ip_info.usage}")
            return HoverInfoProvider.create_hover_info(description)

        # Handle HTTP control patterns (http.cntl.*)
        if full_expression.startswith('http.cntl.'):
            cntl_field = full_expression[10:].upper()

            # Use comprehensive HTTP control documentation
            if cntl_field in doc.LSP_HTTP_CONTROL_DOCUMENTATION:
                cntl_doc = doc.LSP_HTTP_CONTROL_DOCUMENTATION[cntl_field]
                description = (
                    f"**{full_expression}** - {cntl_doc.name}\n\n"
                    f"**Description:** {cntl_doc.description}\n\n"
                    f"**Default Value:** `{cntl_doc.default_value}`\n\n"
                    f"**Usage:** {cntl_doc.usage}")

                if cntl_doc.examples:
                    description += "\n\n**Examples:**\n"
                    for example in cntl_doc.examples:
                        description += f"```hrw4u\n{example}\n```\n"

                return HoverInfoProvider.create_hover_info(description)

            # Fallback for undocumented control fields
            else:
                description = (
                    f"**{full_expression}** - HTTP Transaction Control Field\n\n"
                    f"**Context:** HTTP Transaction Control\n\n"
                    f"**Description:** Controls ATS transaction processing behavior\n\n"
                    "Used for fine-grained control of HTTP transaction processing.")
                return HoverInfoProvider.create_hover_info(description)

        if full_expression in ['inbound.req', 'inbound.resp', 'outbound.req', 'outbound.resp']:
            return OperatorHoverProvider.get_operator_hover_info(full_expression)

        if '.' in full_expression:
            return OperatorHoverProvider.get_operator_hover_info(full_expression)

        return None

    @staticmethod
    def _handle_pattern_match(match, full_expression: str, cursor_pos: int, expr_start: int) -> Dict[str, Any] | None:
        """Handle a matched pattern for dotted expressions."""
        pattern_len = len(match.pattern)

        # If cursor is within the pattern prefix, show pattern context
        if cursor_pos < pattern_len:
            context = match.pattern.rstrip('.')

            # Special handling for cookie patterns - use comprehensive documentation
            if match.context_type == 'Cookie' and context in doc.LSP_COOKIE_DOCUMENTATION:
                cookie_doc = doc.LSP_COOKIE_DOCUMENTATION[context]
                description = (
                    f"**{cookie_doc.name}** - {context}\n\n"
                    f"**Context:** {cookie_doc.context}\n\n"
                    f"**Description:** {cookie_doc.description}\n\n"
                    f"**Usage:** {cookie_doc.usage}")

                if cookie_doc.examples:
                    description += "\n\n**Examples:**\n"
                    for example in cookie_doc.examples:
                        description += f"```hrw4u\n{example}\n```\n"

                return HoverInfoProvider.create_hover_info(description)

            return OperatorHoverProvider.get_operator_hover_info(context)

        # Cursor is in the suffix - handle specific field types
        if match.suffix:
            if match.context_type == 'Certificate':
                return CertificateHoverProvider.parse_certificate_expression(full_expression, is_interpolation=False)

            elif match.context_type == 'Header':
                return DottedExpressionHoverProvider._handle_header_suffix(match, full_expression)

            elif match.context_type == 'Cookie':
                return DottedExpressionHoverProvider._handle_cookie_suffix(match, full_expression)

            elif match.context_type == 'Connection' and match.field_dict_key:
                return DottedExpressionHoverProvider._handle_connection_suffix(match, full_expression)

            elif match.field_dict_key:
                # Handle field patterns (now., id., geo.)
                return DottedExpressionHoverProvider._handle_field_suffix(match, full_expression)

        return None

    @staticmethod
    def _handle_header_suffix(match, full_expression: str) -> Dict[str, Any] | None:
        """Handle header field suffix."""
        header_name = match.suffix
        if header_name:
            is_ats_internal = header_name.startswith('@')
            header_type = "ATS Internal Header" if is_ats_internal else "HTTP Header"
            context_desc = match.pattern.rstrip('.').replace('.', ' ').title()

            if is_ats_internal:
                additional_info = "This is an ATS internal header (prefixed with '@') used for internal processing and metadata."
            else:
                additional_info = "HTTP header used in request/response processing."

            description = (
                f"**{header_name}** - {header_type}\n\n"
                f"**Context:** {context_desc} Headers\n\n"
                f"{additional_info}")

            return HoverInfoProvider.create_hover_info(description)
        return None

    @staticmethod
    def _handle_cookie_suffix(match, full_expression: str) -> Dict[str, Any] | None:
        """Handle cookie field suffix."""
        cookie_name = match.suffix
        if cookie_name:
            direction = "Inbound" if match.pattern.startswith('inbound') else "Outbound"
            action = "received from client" if match.pattern.startswith('inbound') else "sent to client"
            description = (
                f"**{cookie_name}** - HTTP Cookie\n\n"
                f"**Context:** {direction} Request Cookie\n\n"
                f"**Description:** HTTP cookie named '{cookie_name}' {action}. "
                f"Used for session management, user authentication, preferences, and client state tracking.\n\n"
                f"**Usage:** Access or modify this specific cookie value for request processing.")

            return HoverInfoProvider.create_hover_info(description)
        return None

    @staticmethod
    def _handle_connection_suffix(match, full_expression: str) -> Dict[str, Any] | None:
        """Handle connection field suffix."""
        parts = full_expression.split('.')
        if len(parts) == 3:
            direction = parts[0].title()
            field = parts[2].upper()

            field_dict = getattr(doc, match.field_dict_key)
            if field in field_dict:
                field_info = field_dict[field]
                description = (
                    f"**{parts[2]}** - {field_info.name}\n\n"
                    f"**Context:** {direction} Connection {field_info.name}\n\n"
                    f"**Description:** {field_info.description}\n\n"
                    f"**Maps to:** `set-conn-{parts[2].lower()}`")

                return HoverInfoProvider.create_hover_info(description)
        return None

    @staticmethod
    def _handle_field_suffix(match, full_expression: str) -> Dict[str, Any] | None:
        """Handle field suffix for now., id., geo. patterns."""
        field_dict = getattr(doc, match.field_dict_key)
        suffix_key = match.suffix.upper()
        if suffix_key in field_dict:
            field_info = field_dict[suffix_key]
            return HoverInfoProvider.create_field_hover(
                full_expression, field_info.name, field_info.description, match.context_type, match.maps_to or
                f"%{{{match.context_type.upper()}:{suffix_key}}}",
                f"Used for {match.context_type.lower()} information and conditional logic.")
        return None


class OperatorHoverProvider:
    """Specialized hover provider for operators and conditions."""

    SIMILARITY_THRESHOLD = 2

    @classmethod
    def _get_known_prefixes(cls) -> set[str]:
        """Extract known prefixes from existing documentation and operator tables."""
        prefixes = set()

        # Extract from namespace documentation
        if hasattr(doc, 'LSP_NAMESPACE_DOCUMENTATION'):
            prefixes.update(doc.LSP_NAMESPACE_DOCUMENTATION.keys())

        # Extract from sub-namespace documentation
        if hasattr(doc, 'LSP_SUB_NAMESPACE_DOCUMENTATION'):
            for key in doc.LSP_SUB_NAMESPACE_DOCUMENTATION.keys():
                prefixes.add(key.split('.')[0])

        # Extract from operator map
        for key in OPERATOR_MAP.keys():
            if '.' in key:
                prefixes.add(key.split('.')[0])

        # Extract from condition map
        for key in CONDITION_MAP.keys():
            if '.' in key:
                prefixes.add(key.split('.')[0])

        return prefixes

    @staticmethod
    def get_operator_hover_info(operator: str) -> Dict[str, Any]:
        """Get hover info for operators."""
        # Handle method field with comprehensive documentation
        if operator == "inbound.method":
            if "method" in doc.LSP_METHOD_DOCUMENTATION:
                method_doc = doc.LSP_METHOD_DOCUMENTATION["method"]
                description = (
                    f"**{operator}** - {method_doc.name}\n\n"
                    f"**Description:** {method_doc.description}\n\n"
                    f"**Usage:** {method_doc.usage}")

                if method_doc.examples:
                    description += "\n\n**Examples:**\n"
                    for example in method_doc.examples:
                        description += f"```hrw4u\n{example}\n```\n"

                return HoverInfoProvider.create_hover_info(description)
        # Handle any .url patterns generically
        if operator.endswith('.url'):
            prefix = operator.replace('.url', '').title()
            return HoverInfoProvider.create_hover_info(
                f"**{operator}** - HRW4U URL Context\n\n"
                f"**Context:** {prefix} URL Components\n\n"
                f"Use this prefix to access URL components. Available: host, port, path, query, scheme, fragment.\n\n"
                f"**Example:** `{operator}.host` for the hostname portion of the URL.")

        # Special handling for inbound/outbound header contexts
        if operator in doc.LSP_SUB_NAMESPACE_DOCUMENTATION:
            sub_namespace_doc = doc.LSP_SUB_NAMESPACE_DOCUMENTATION[operator]
            sections = [
                f"**{operator}** - {sub_namespace_doc.name}", "", f"**Context:** {sub_namespace_doc.context}", "",
                f"**Description:** {sub_namespace_doc.description}", "",
                f"**Available items:** {', '.join(sub_namespace_doc.available_items)}", "", f"**Usage:** {sub_namespace_doc.usage}"
            ]
            if sub_namespace_doc.examples:
                sections.extend(["", "**Examples:**"])
                for example in sub_namespace_doc.examples:
                    sections.append(f"```hrw4u\n{example}\n```")
            return HoverInfoProvider.create_hover_info("\n".join(sections))

        # Check exact matches first
        if operator in OPERATOR_MAP:
            commands, _, is_prefix, sections = OPERATOR_MAP[operator]
            cmd_str = commands if isinstance(commands, str) else ' / '.join(commands)

            section_info = ""
            if sections:
                section_names = [s.value for s in sections]
                section_info = f"\n\n**Restricted in sections:** {', '.join(section_names)}"

            return HoverInfoProvider.create_hover_info(
                f"**{operator}** - HRW4U Operator\n\n" + f"**Maps to:** `{cmd_str}`{section_info}")

        # Check prefix matches
        for key, (commands, _, is_prefix, sections) in OPERATOR_MAP.items():
            if is_prefix and operator.startswith(key):
                cmd_str = commands if isinstance(commands, str) else ' / '.join(commands)
                suffix = operator[len(key):]

                section_info = ""
                if sections:
                    section_names = [s.value for s in sections]
                    section_info = f"\n\n**Restricted in sections:** {', '.join(section_names)}"

                return HoverInfoProvider.create_hover_info(
                    f"**{operator}** - HRW4U Operator\n\n" + f"**Base:** `{key}`\n" + f"**Suffix:** `{suffix}`\n" +
                    f"**Maps to:** `{cmd_str}`{section_info}")

        # Check condition map
        if operator in CONDITION_MAP:
            tag, _, is_prefix, sections, _, _ = CONDITION_MAP[operator]

            section_info = ""
            if sections:
                section_names = [s.value for s in sections]
                section_info = f"\n\n**Restricted in sections:** {', '.join(section_names)}"

            return HoverInfoProvider.create_hover_info(
                f"**{operator}** - HRW4U Condition\n\n" + f"**Maps to:** `{tag}`{section_info}")

        # Check condition prefix matches
        for key, (tag, _, is_prefix, sections, is_conditional, _) in CONDITION_MAP.items():
            if is_prefix and operator.startswith(key):
                suffix = operator[len(key):]

                section_info = ""
                if sections:
                    section_names = [s.value for s in sections]
                    section_info = f"\n\n**Restricted in sections:** {', '.join(section_names)}"

                return HoverInfoProvider.create_hover_info(
                    f"**{operator}** - HRW4U Condition\n\n" + f"**Base:** `{key}`\n" + f"**Suffix:** `{suffix}`\n" +
                    f"**Maps to:** `{tag}`{section_info}")

        # Handle namespace prefixes with comprehensive documentation as fallback
        namespace_info = OperatorHoverProvider._get_namespace_hover_info(operator)
        if namespace_info:
            return namespace_info

        known_prefixes = OperatorHoverProvider._get_known_prefixes()

        for prefix in known_prefixes:
            if abs(len(operator) - len(prefix)) <= OperatorHoverProvider.SIMILARITY_THRESHOLD:
                matches = sum(1 for a, b in zip(operator.lower(), prefix) if a == b)
                if matches >= len(prefix) - OperatorHoverProvider.SIMILARITY_THRESHOLD:
                    return None

        if ('_' in operator or len(operator) > 8) and not any(operator.lower().startswith(p[:3]) for p in known_prefixes):
            return HoverInfoProvider.create_hover_info(f"**{operator}** - HRW4U symbol")

        return None

    @staticmethod
    def _get_namespace_hover_info(operator: str) -> Dict[str, Any] | None:
        """Get comprehensive hover info for namespace prefixes using centralized documentation."""
        # Strip trailing dot for namespace lookup (handles cases like "inbound." -> "inbound")
        namespace_key = operator.rstrip('.')

        # First check for sub-namespace patterns (e.g., "inbound.conn", "outbound.req")
        if namespace_key in doc.LSP_SUB_NAMESPACE_DOCUMENTATION:
            sub_namespace_doc = doc.LSP_SUB_NAMESPACE_DOCUMENTATION[namespace_key]

            # Build the hover content from the sub-namespace documentation
            sections = [
                f"**{namespace_key}** - {sub_namespace_doc.name}", "", f"**Context:** {sub_namespace_doc.context}", "",
                f"**Description:** {sub_namespace_doc.description}", "",
                f"**Available items:** {', '.join(sub_namespace_doc.available_items)}", "", f"**Usage:** {sub_namespace_doc.usage}"
            ]

            if sub_namespace_doc.examples:
                sections.extend(["", "**Examples:**"])
                for example in sub_namespace_doc.examples:
                    sections.append(f"```hrw4u\n{example}\n```")

            return HoverInfoProvider.create_hover_info("\n".join(sections))

        # For single-part namespace documentation, show it unless it's a known condition that should
        # take precedence (like standalone "now" in conditional contexts)
        # This allows namespace documentation for "geo", "id", etc. while preserving condition behavior
        pass  # No additional filtering - let the fallback logic in get_operator_hover_info handle it

        # Fall back to single-part namespace documentation
        if namespace_key not in doc.LSP_NAMESPACE_DOCUMENTATION:
            return None

        namespace_doc = doc.LSP_NAMESPACE_DOCUMENTATION[namespace_key]

        # Build the hover content from the centralized documentation
        sections = [
            f"**{namespace_key}** - {namespace_doc.name}", "", f"**Context:** {namespace_doc.context}", "",
            f"**Description:** {namespace_doc.description}", "", f"**Available items:** {', '.join(namespace_doc.available_items)}",
            "", f"**Usage:** {namespace_doc.usage}"
        ]

        if namespace_doc.examples:
            sections.extend(["", "**Examples:**"])
            for example in namespace_doc.examples:
                sections.append(f"```hrw4u\n{example}\n```")

        return HoverInfoProvider.create_hover_info("\n".join(sections))


class RegexHoverProvider:
    """Specialized hover provider for regular expression patterns."""

    @staticmethod
    def get_regex_hover_info(line: str, character: int) -> Dict[str, Any] | None:
        """Get hover info for regex patterns with brief LSP-appropriate documentation."""
        regex_data = doc.RegexPattern.detect_regex_pattern(line, character)
        if regex_data:
            hover_text = doc.RegexPattern.create_regex_hover(regex_data, brief=True)
            return HoverInfoProvider.create_hover_info(hover_text)
        return None


class FunctionHoverProvider:
    """Specialized hover provider for functions."""

    @staticmethod
    def get_function_hover_info(function_name: str) -> Dict[str, Any]:
        """Get hover info for functions with comprehensive documentation."""
        # Check comprehensive documentation first
        if function_name in doc.LSP_FUNCTION_DOCUMENTATION:
            func_doc = doc.LSP_FUNCTION_DOCUMENTATION[function_name]

            # Build parameter documentation
            param_docs = []
            if func_doc.parameters:
                param_docs.append("**Parameters:**")
                for param in func_doc.parameters:
                    param_docs.append(f"- `{param.name}` ({param.type}): {param.description}")

            # Build examples section
            example_docs = []
            if func_doc.examples:
                example_docs.append("**Examples:**")
                for example in func_doc.examples:
                    example_docs.append(f"```hrw4u\n{example}\n```")

            # Combine all documentation
            sections = [
                f"**{func_doc.name}** - {func_doc.category}", "", func_doc.description, "", f"**Syntax:** `{func_doc.syntax}`"
            ]

            if param_docs:
                sections.extend([""] + param_docs)

            if example_docs:
                sections.extend([""] + example_docs)

            sections.extend(["", f"**Maps to:** `{func_doc.maps_to}`", "", f"**Usage:** {func_doc.usage_context}"])

            # Add return values if available (for condition functions)
            if func_doc.return_values:
                sections.extend(["", f"**Return Values:** {', '.join(func_doc.return_values)}"])

            return HoverInfoProvider.create_hover_info("\n".join(sections))

        # Fallback to basic documentation
        if function_name in FUNCTION_MAP:
            tag, _ = FUNCTION_MAP[function_name]
            return HoverInfoProvider.create_hover_info(
                f"**{function_name}()** - HRW4U Function\n\n" + f"**Maps to:** `{tag}`\n\n" + f"Used in conditional expressions.")

        if function_name in STATEMENT_FUNCTION_MAP:
            tag, _ = STATEMENT_FUNCTION_MAP[function_name]
            return HoverInfoProvider.create_hover_info(
                f"**{function_name}()** - HRW4U Statement Function\n\n" + f"**Maps to:** `{tag}`\n\n" +
                f"Used as statements in code blocks.")

        return HoverInfoProvider.create_hover_info(f"**{function_name}()** - Unknown HRW4U function")


class VariableHoverProvider:
    """Specialized hover provider for variables and variable types."""

    @staticmethod
    def get_variable_type_hover_info(var_type: str) -> Dict[str, Any]:
        """Get hover info for variable types."""
        try:
            vt = VarType.from_str(var_type.lower())
            return HoverInfoProvider.create_hover_info(
                f"**{var_type}** - HRW4U Variable Type\n\n{vt.description}\n\nUsed in variable declarations within the VARS section."
            )
        except ValueError:
            return HoverInfoProvider.create_hover_info(f"**{var_type}** - Unknown Variable Type")

    @staticmethod
    def get_variable_hover_info(variable_declarations: Dict[str, Dict[str, Any]], uri: str,
                                variable_name: str) -> Dict[str, Any] | None:
        """Get hover info for declared variables."""
        variables = variable_declarations.get(uri, {})
        if variable_name in variables:
            var_info = variables[variable_name]
            var_type = var_info['type']
            description = var_info['description']
            declaration_line = var_info['line'] + 1

            return HoverInfoProvider.create_hover_info(
                f"**{variable_name}** - HRW4U Variable\n\n" + f"**Type:** `{var_type}`\n\n" +
                f"**Description:** {description}\n\n" + f"**Declared at:** Line {declaration_line}")
        return None


class SectionHoverProvider:
    """Specialized hover provider for sections."""

    @staticmethod
    def get_section_hover_info(section_name: str) -> Dict[str, Any]:
        """Get hover info for section names."""
        # Don't treat regex patterns as section names!
        if section_name.startswith('/') or section_name.startswith('~') or '(' in section_name or ')' in section_name:
            return HoverInfoProvider.create_hover_info(
                f"**{section_name}** - Regular Expression Pattern\n\n"
                f"**Context:** Pattern Matching Expression\n\n"
                f"Regular expression used for string matching in conditions. "
                f"Supports Perl-compatible regex syntax including case-insensitive matching (?i), anchors (^ $), and character classes."
            )

        try:
            section_type = SectionType(section_name)
            return HoverInfoProvider.create_hover_info(
                f"**{section_name}** - HRW4U Section\n\n" + f"**Hook:** `{section_type.hook_name}`\n\n" +
                section_type.lsp_description)
        except ValueError:
            return HoverInfoProvider.create_hover_info(
                f"**{section_name}** - ❌ Invalid Section Name\n\n" +
                f"Valid sections are: {', '.join([s.value for s in SectionType])}")


class ModifierHoverProvider:
    """Specialized hover provider for condition modifiers used with 'with' keyword."""

    @staticmethod
    def get_modifier_hover_info(line: str, character: int) -> Dict[str, Any] | None:
        """Get hover info for condition modifiers in 'with' clauses."""
        modifier_data = doc.ModifierPattern.detect_modifier_list(line, character)
        if modifier_data:
            hover_text = doc.ModifierPattern.create_modifier_hover(modifier_data)
            return HoverInfoProvider.create_hover_info(hover_text)
        return None
