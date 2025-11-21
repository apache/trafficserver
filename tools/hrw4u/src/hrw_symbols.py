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
from typing import Callable
from functools import cached_property
import re

from hrw4u.errors import SymbolResolutionError
from hrw4u.validation import Validator
import hrw4u.types as types
import hrw4u.tables as tables
from hrw4u.states import SectionType
from hrw4u.symbols_base import SymbolResolverBase


class InverseSymbolResolver(SymbolResolverBase):
    """Reverse mapping utilities for hrw4u output generation."""

    def __init__(self) -> None:
        super().__init__(debug=False)  # Default to no debug for inverse resolver
        self._state_vars: dict[tuple[types.VarType, int], str] = {}

    @cached_property
    def _rev_conditions_exact(self) -> dict[str, str]:
        """Cached reverse condition mapping for exact matches."""
        if reverse_map := tables.REVERSE_RESOLUTION_MAP.get('EXACT_CONDITIONS'):
            return reverse_map
        # Fallback to building from condition map if not available
        result = {}
        for ident_key, params in self._condition_map.items():
            if not ident_key.endswith("."):
                tag = params.target
                tag_key = tag.strip().removeprefix("%{").removesuffix("}").split(":", 1)[0]
                result[tag_key] = ident_key
        return result

    @cached_property
    def _rev_conditions_prefix(self) -> list[tuple[str, str, bool]]:
        """Cached reverse condition mapping for prefix matches."""
        if reverse_map := tables.REVERSE_RESOLUTION_MAP.get('PREFIX_CONDITIONS'):
            return reverse_map
        # Fallback to building from condition map if not available
        result = []
        for ident_key, params in self._condition_map.items():
            if ident_key.endswith("."):
                tag = params.target
                uppercase = params.upper if params else False
                result.append((tag, ident_key, uppercase))
        return result

    @cached_property
    def _rev_functions(self) -> dict[str, str]:
        """Cached reverse function mapping."""
        if reverse_map := tables.REVERSE_RESOLUTION_MAP.get('FUNCTIONS'):
            return reverse_map
        return {params.target: fn_name for fn_name, params in self._function_map.items()}

    @cached_property
    def _rev_sections(self) -> dict[str, str]:
        """Cached reverse section mapping."""
        return {s.hook_name: s.value for s in SectionType}

    def _section_label_for_tag(self, tag: str) -> str | None:
        try:
            return SectionType.from_hook(tag).value
        except ValueError:
            return None

    def _resolve_status_target(self, section: SectionType | None) -> str:
        if section and (status_targets := tables.REVERSE_RESOLUTION_MAP.get("STATUS_TARGETS")):
            for section_set, target in status_targets.items():
                if section in section_set:
                    return target
        return "inbound.status"

    def _resolve_fallback_tag(self, tag: str, payload: str, section: SectionType | None) -> tuple[str, bool] | None:
        if (fallback_map := tables.REVERSE_RESOLUTION_MAP.get("FALLBACK_TAG_MAP")) and (tag_info := fallback_map.get(tag)):
            context_info, use_payload = tag_info

            if use_payload:
                prefix = self.get_prefix_for_context(context_info, section)
                return f"{prefix}{payload}", False
            return f"{context_info}{payload}", False
        return None

    def _get_or_create_var_name(self, var_type: types.VarType, index: int) -> str:
        key = (var_type, index)
        if key not in self._state_vars:
            type_name = var_type.name.lower()
            self._state_vars[key] = f"{type_name}_{index}"
        return self._state_vars[key]

    def _resolve_from_context_map(self, context_map: dict, section: SectionType | None, default: str) -> str:
        if section:
            if section in context_map:
                return context_map[section]
            for key, value in context_map.items():
                if isinstance(key, frozenset) and section in key:
                    return value
        return default

    def get_prefix_for_context(self, context_type: str, section: SectionType | None) -> str:
        if (context_map := tables.REVERSE_RESOLUTION_MAP.get("CONTEXT_TYPE_MAP")) and (mapping := context_map.get(context_type)):
            if isinstance(mapping, str):
                return mapping

            map_name, default = mapping
            if resolved_map := tables.REVERSE_RESOLUTION_MAP.get(map_name):
                return self._resolve_from_context_map(resolved_map, section, default)

        return "inbound.resp."

    def _resolve_ambiguous_exact(self, tag: str, section: SectionType | None) -> str | None:
        if tag == "STATUS":
            if (mapping := tables.REVERSE_RESOLUTION_MAP.get("STATUS")):
                outbound_sections = mapping["outbound_sections"]
                if section in outbound_sections or section == SectionType.READ_RESPONSE:
                    return mapping["outbound_result"]
                return mapping["inbound_result"]
        elif tag == "METHOD":
            if (mapping := tables.REVERSE_RESOLUTION_MAP.get("METHOD")):
                outbound_sections = mapping["outbound_sections"]
                if section in outbound_sections:
                    return mapping["outbound_result"]
                return mapping["inbound_result"]
        elif tag == "IP":
            return None

        for key, params in self._condition_map.items():
            mapped_tag = params.target
            tag_part = mapped_tag.replace("%{", "").replace("}", "").split(":")[0]
            restricted = params.sections if params else None
            if tag_part == tag:
                if not restricted or not section or section not in restricted:
                    pass

        return None

    def _handle_state_tag(self, tag: str, payload: str | None) -> tuple[str, bool]:
        state_type = tag[6:]
        if payload is None:
            raise SymbolResolutionError(f"%{{{tag}}}", f"Missing index for {tag}")
        try:
            index = int(payload)
            for var_type in types.VarType:
                if var_type.cond_tag == state_type:
                    return self._get_or_create_var_name(var_type, index), False
            raise SymbolResolutionError(f"%{{{tag}}}", f"Unknown state type: {state_type}")
        except ValueError:
            raise SymbolResolutionError(f"%{{{tag}}}", f"Invalid index for {tag}: {payload}")

    def _handle_ip_tag(self, payload: str) -> tuple[str | None, bool]:
        if (ip_map := tables.REVERSE_RESOLUTION_MAP.get("IP")) and (result := ip_map.get(payload)):
            return result, False
        return None, False

    def _handle_prefix_conditions(self, tag: str, payload: str, section: SectionType | None) -> tuple[str, bool] | None:
        for tag_match, lhs_prefix, needs_upper in self._rev_conditions_prefix:
            if tag_match == tag:
                suffix = payload.upper() if needs_upper else payload

                # Use existing type definitions for case handling
                if self._should_lowercase_suffix(tag_match, lhs_prefix):
                    suffix = suffix.lower()

                if tag == "HEADER":
                    return f"{self.get_prefix_for_context('header_condition', section)}{suffix}", False
                else:
                    return f"{lhs_prefix}{suffix}", False
        return None

    def _should_lowercase_suffix(self, tag_match: str, lhs_prefix: str) -> bool:
        """Determine if suffix should be lowercase based on field type."""
        # URL fields should be lowercase (use existing SuffixGroup)
        if lhs_prefix.endswith(".url.") or tag_match in {"CLIENT-URL", "TO-URL", "FROM-URL", "NEXT-HOP"}:
            return True
        # Time fields should be lowercase
        if tag_match == "NOW":
            return True
        return False

    def _rewrite_inline_percents(self, value: str, section: SectionType | None) -> str:
        # Handle simple cases first
        if types.BooleanLiteral.contains(value):
            return value.lower()
        if value.isnumeric():
            return value

        # Handle full percent block
        if value.startswith('%{') and value.endswith('}') and Validator._PERCENT_RE.fullmatch(value):
            try:
                expr, _ = self.percent_to_ident_or_func(value, section)
                return f'"{{{expr}}}"'
            except SymbolResolutionError:
                return f'"{value}"'

        # Handle quoted strings with embedded percent blocks
        is_quoted = value.startswith('"') and value.endswith('"')
        inner_value = value[1:-1] if is_quoted else value

        rewritten = Validator._PERCENT_PATTERN.sub(self._make_percent_replacer(section), inner_value)
        return f'"{rewritten}"'

    def _make_percent_replacer(self, section: SectionType | None):
        """Create a replacement function for percent blocks in strings."""

        def repl(match: re.Match) -> str:
            try:
                expr, _ = self.percent_to_ident_or_func(match.group(0), section)
                return "{" + expr + "}"
            except SymbolResolutionError:
                return match.group(0)

        return repl

    def _handle_set_rm_operation(self, cmd: str, toks: list[str], prefix: str, qualifier: str, context: str) -> str:
        if cmd.startswith("rm-"):
            return f'{prefix}{qualifier} = ""'
        if len(toks) < 3:
            raise SymbolResolutionError(" ".join(toks), f"Missing value for {cmd}")
        value = " ".join(toks[2:])
        value = self._rewrite_inline_percents(value, None)
        return f"{prefix}{qualifier} = {value}"

    def _handle_operator_command(
            self, cmd: str, toks: list[str], lhs_key: str, uppercase: bool, section: SectionType | None) -> str:
        if cmd == "set-status":
            if len(toks) < 2:
                raise SymbolResolutionError(" ".join(toks), "Missing value")
            value = self._rewrite_inline_percents(" ".join(toks[1:]), section)
            target = self._resolve_status_target(section)
            return f"{target} = {value}"

        if cmd in tables.OPERATOR_COMMAND_MAP:
            if len(toks) < 2:
                op_type = cmd.split('-')[1]
                raise SymbolResolutionError(" ".join(toks), f"Missing {op_type} name")

            context_type, op_context, qualifier_extractor, qualifier_processor = tables.OPERATOR_COMMAND_MAP[cmd]
            qualifier = qualifier_extractor(toks)

            prefix = self.get_prefix_for_context(context_type, section)

            processed_qualifier = qualifier_processor(qualifier)
            return self._handle_set_rm_operation(cmd, toks, prefix, processed_qualifier, op_context)

        if lhs_key.endswith("."):
            if len(toks) < 2:
                raise SymbolResolutionError(" ".join(toks), "Missing qualifier")
            qualifier_out = toks[1].upper() if uppercase else toks[1]
            if cmd.startswith("rm-"):
                return f'{lhs_key}{qualifier_out} = ""'
            if len(toks) < 3:
                raise SymbolResolutionError(" ".join(toks), "Missing value")
            value = self._rewrite_inline_percents(" ".join(toks[2:]), section)
            return f"{lhs_key}{qualifier_out} = {value}"

        if len(toks) < 2:
            raise SymbolResolutionError(" ".join(toks), "Missing value")
        value = self._rewrite_inline_percents(" ".join(toks[1:]), section)
        return f"{lhs_key} = {value}"

    def _handle_statement_function(self, name: str, args: list[str], section: SectionType | None, op_state: 'OperatorState') -> str:
        if name == "set-redirect" and len(args) > 1:
            status_code = args[0]
            url_arg = args[1]
            if op_state.qsa:
                url_arg += '?%{CLIENT-URL:QUERY}'

            qargs = [status_code, self._rewrite_inline_percents(f'"{url_arg}"', section)]
        elif name == "add-header" and args:
            # Convert add-header command to += syntax for reverse mapping
            header_name = args[0]
            prefix = self.get_prefix_for_context("header_ops", section)
            prefixed_header = f"{prefix}{header_name}"

            if len(args) > 1:
                value = self._rewrite_inline_percents(args[1], section)
                return f"{prefixed_header} += {value}"
            raise SymbolResolutionError("add-header", "Missing value for add-header")
        elif name == "set-plugin-cntl" and len(args) >= 2:
            qualifier = args[0]
            value = args[1]
            # Always quote the value for consistent output
            quoted_value = f'"{value}"' if not (value.startswith('"') and value.endswith('"')) else value
            qargs = [qualifier, quoted_value]
        else:
            qargs = [self._rewrite_inline_percents(Validator.quote_if_needed(a), section) for a in args]

        return f"{name}({', '.join(qargs)})" if qargs else f"{name}()"

    def parse_percent_block(self, pct: str) -> tuple[str, str | None]:
        """Parse percent block into tag and payload components."""
        try:
            Validator.percent_block()(pct)
        except Exception:
            return pct, None

        inner = pct[2:]
        if inner.endswith("}}"):
            inner = inner[:-2]
        elif inner.endswith("}"):
            inner = inner[:-1]
        if ":" in inner:
            # Check for multi-part certificate tags first
            if (fallback_map := tables.REVERSE_RESOLUTION_MAP.get("FALLBACK_TAG_MAP")):
                parts = inner.split(":")
                for i in range(len(parts) - 1, 0, -1):
                    potential_tag = ":".join(parts[:i])
                    if potential_tag in fallback_map:
                        remaining_parts = parts[i:]
                        payload = ":".join(remaining_parts) if remaining_parts else None
                        return potential_tag, payload

            # Default behavior - split on first colon
            tag, payload = inner.split(":", 1)
            return tag, payload
        return inner, None

    def convert_set_to_brackets(self, set_text: str) -> str:
        """Convert set notation to bracket format."""
        try:
            Validator.set_format()(set_text)
        except Exception:
            return set_text

        if set_text.startswith('(') and set_text.endswith(')'):
            content = set_text[1:-1]
            content = ', '.join(item.strip() for item in content.split(','))
            return '[' + content + ']'
        elif set_text.startswith('[') and set_text.endswith(']'):
            content = set_text[1:-1]
            content = ', '.join(item.strip() for item in content.split(','))
            return '[' + content + ']'
        return set_text

    def format_iprange(self, iprange_text: str) -> str:
        """Format IP range with proper spacing."""
        try:
            Validator.iprange_format()(iprange_text)
        except Exception:
            return iprange_text

        if iprange_text.startswith('{') and iprange_text.endswith('}'):
            content = iprange_text[1:-1]
            content = ', '.join(item.strip() for item in content.split(','))
            return '{' + content + '}'
        return iprange_text

    def get_var_declarations(self) -> list[str]:
        """Get variable declarations in hrw4u format."""
        declarations = []
        for (var_type, _), var_name in sorted(self._state_vars.items(), key=lambda x: (x[0][0].name, x[0][1])):
            declarations.append(f"{var_name}: {var_type.name.lower()};")
        return declarations

    def negate_expression(self, term: str) -> str:
        """Negate a logical expression appropriately."""
        t = term.strip()
        if ' == ""' in t:
            return t.replace(' == ""', '')
        if ' != ""' in t:
            return f"!{t.replace(' != \"\"', '')}"
        if "==" in t:
            return t.replace("==", "!=", 1)
        if " !=" in t or "!=" in t:
            return t
        if " ~ " in t and " !~ " not in t:
            return t.replace(" ~ ", " !~ ", 1)
        if any(op in t for op in (" in ", " > ", " < ")):
            return f"!({t})"
        if t.endswith(')'):
            return f"!{t}"
        if " " not in t:
            return f"!{t}"
        return f"!({t})"

    def percent_to_ident_or_func(self, percent: str, section: SectionType | None) -> tuple[str, bool]:
        """Convert percent block to identifier or function call."""
        match = Validator._PERCENT_RE.match(percent)
        if not match:
            raise SymbolResolutionError(percent, "Invalid %{...} reference")

        tag = match.group(1)
        payload = match.group(2)

        # Handle certificate tags explicitly to ensure proper parsing
        original_inner = percent[2:-1]
        if ":" in original_inner and any(cert_tag in original_inner for cert_tag in ["CLIENT-CERT", "SERVER-CERT"]):
            new_tag, new_payload = self.parse_percent_block(percent)
            if new_tag != tag or new_payload != payload:
                tag, payload = new_tag, new_payload

        if types.BooleanLiteral.contains(tag):
            return tag, False

        if self._section_label_for_tag(tag) is not None:
            return percent, False

        if tag.startswith("STATE-"):
            return self._handle_state_tag(tag, payload)

        if tag == "IP" and payload:
            result = self._handle_ip_tag(payload)
            if result[0] is not None:
                return result

        if tag in self._rev_functions:
            fn = self._rev_functions[tag]
            if payload is not None:
                parts = [p.strip() for p in payload.split(",")]
                args = [Validator.quote_if_needed(p) for p in parts if p != ""]
                return f"{fn}({', '.join(args)})", True
            return f"{fn}()", True

        if payload is not None:
            result = self._handle_prefix_conditions(tag, payload, section)
            if result is not None:
                return result

        if tag in self._rev_conditions_exact:
            special = self._resolve_ambiguous_exact(tag, section)
            return (special, False) if special is not None else (self._rev_conditions_exact[tag], False)

        if payload is None:
            special = self._resolve_ambiguous_exact(tag, section)
            if special is not None:
                return special, False
            raise SymbolResolutionError(percent, "Missing payload for prefix condition")

        result = self._resolve_fallback_tag(tag, payload, section)
        if result is not None:
            return result

        raise SymbolResolutionError(percent, f"Unknown percent tag: {tag}")

    def op_to_hrw4u(self, cmd: str, args: list[str], section: SectionType | None, op_state: 'OperatorState') -> str:
        """Convert HRW operation to hrw4u statement."""
        if cmd == "no-op" and op_state.last:
            return "break"

        if cmd == "rm-destination" and args and args[0].upper() == "QUERY":
            if len(args) > 1:
                func = "keep_query" if op_state.invert else "remove_query"
                return f"{func}({self._rewrite_inline_percents(args[1], section)})"
            return 'inbound.url.query = ""'

        toks = [cmd] + args
        line = " ".join(toks)

        for var_type in types.VarType:
            if cmd == var_type.op_tag:
                if len(toks) < 3:
                    raise SymbolResolutionError(line, f"Missing arguments for {cmd}")
                try:
                    index = int(toks[1])
                    value = " ".join(toks[2:])
                except ValueError:
                    raise SymbolResolutionError(line, f"Invalid index for {cmd}: {toks[1]}")

                var_name = self._get_or_create_var_name(var_type, index)
                if value.startswith('%{') and value.endswith('}'):
                    rewritten_value, _ = self.percent_to_ident_or_func(value, section)
                else:
                    rewritten_value = self._rewrite_inline_percents(value, section)
                return f"{var_name} = {rewritten_value}"

        for lhs_key, params in tables.OPERATOR_MAP.items():
            commands = params.target if params else None
            if (isinstance(commands, (list, tuple)) and cmd in commands) or (cmd == commands):
                uppercase = params.upper if params else False
                return self._handle_operator_command(cmd, toks, lhs_key, uppercase, section)

        for name, params in tables.STATEMENT_FUNCTION_MAP.items():
            if params.target == cmd:
                return self._handle_statement_function(name, args, section, op_state)

        raise SymbolResolutionError(line, f"Unknown operator: {cmd}")
