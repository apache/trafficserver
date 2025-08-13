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


class InverseSymbolResolver:
    """
    Reverse mapping utilities driven by the forward SymbolResolver tables.
    Designed to produce hrw4u output that round-trips closely (including
    naming/casing where hrw4u establishes a style).
    """

    def __init__(self):
        self._state_vars: dict[tuple[types.VarType, int], str] = {}

    @cached_property
    def _rev_conditions_exact(self) -> dict[str, str]:
        """Cached reverse condition mapping for exact matches"""
        result = {}
        for ident_key, (tag, _, uppercase, *_) in tables.CONDITION_MAP.items():
            if not ident_key.endswith("."):
                tag_key = tag.strip().removeprefix("%{").removesuffix("}").split(":", 1)[0]
                result[tag_key] = ident_key
        return result

    @cached_property
    def _rev_conditions_prefix(self) -> list[tuple[str, str, bool]]:
        """Cached reverse condition mapping for prefix matches"""
        result = []
        for ident_key, (tag, _, uppercase, *_) in tables.CONDITION_MAP.items():
            if ident_key.endswith("."):
                result.append((tag, ident_key, uppercase))
        return result

    @cached_property
    def _rev_functions(self) -> dict[str, str]:
        """Cached reverse function mapping"""
        return {tag: fn_name for fn_name, (tag, _) in tables.FUNCTION_MAP.items()}

    @cached_property
    def _rev_sections(self) -> dict[str, str]:
        """Cached reverse section mapping"""
        return {s.hook_name: s.value for s in SectionType}

    @cached_property
    def _url_tags(self) -> set[str]:
        """Cached URL tags from condition map"""
        return {tag for ident_key, (tag, *_) in tables.CONDITION_MAP.items() if ident_key.endswith(".url.") or "URL" in tag}

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
        if tag in tables.AMBIGUOUS_CONTEXT_TAGS and (mapping := tables.REVERSE_RESOLUTION_MAP.get(tag)):
            outbound_sections = mapping["outbound_sections"]
            if section in outbound_sections or (tag == "STATUS" and section == SectionType.READ_RESPONSE):
                return mapping["outbound_result"]
            return mapping["inbound_result"]
        elif tag == "IP":
            return None

        candidates = []
        for key, (mapped_tag, _, _, restricted, _, _) in tables.CONDITION_MAP.items():
            tag_part = mapped_tag.replace("%{", "").replace("}", "").split(":")[0]
            if tag_part == tag:
                if not restricted or not section or section not in restricted:
                    candidates.append((key, restricted))

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

    def _handle_ip_tag(self, payload: str) -> tuple[str, bool]:
        if (ip_map := tables.REVERSE_RESOLUTION_MAP.get("IP")) and (result := ip_map.get(payload)):
            return result, False
        return None, False

    def _handle_prefix_conditions(self, tag: str, payload: str, section: SectionType | None) -> tuple[str, bool] | None:
        for tag_match, lhs_prefix, needs_upper in self._rev_conditions_prefix:
            if tag_match == tag:
                suffix = payload.upper() if needs_upper else payload
                if lhs_prefix.endswith(".url.") or tag_match in self._url_tags or tag == "NOW":
                    suffix = suffix.lower()
                if tag == "HEADER":
                    return f"{self.get_prefix_for_context('header_condition', section)}{suffix}", False
                return f"{lhs_prefix}{suffix}", False
        return None

    def _rewrite_inline_percents(self, value: str, section: SectionType | None) -> str:
        if types.BooleanLiteral.contains(value):
            return value.lower()

        if value.isnumeric():
            return value

        if value.startswith('%{') and value.endswith('}'):
            m = Validator._PERCENT_RE.fullmatch(value)
            if m:
                try:
                    expr, _ = self.percent_to_ident_or_func(value, section)
                    return f'"{{{expr}}}"'
                except SymbolResolutionError:
                    return f'"{value}"'

        is_quoted = value.startswith('"') and value.endswith('"')
        inner_value = value[1:-1] if is_quoted else value

        def repl(match: re.Match) -> str:
            percent_block = match.group(0)
            try:
                expr, _ = self.percent_to_ident_or_func(percent_block, section)
                return "{" + expr + "}"
            except SymbolResolutionError:
                return percent_block

        rewritten = Validator._PERCENT_PATTERN.sub(repl, inner_value)
        return f'"{rewritten}"'

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
            header_name = args[0]
            prefix = self.get_prefix_for_context("header_ops", section)
            prefixed_header = f"{prefix}{header_name}"

            processed_args = [self._rewrite_inline_percents(arg, section) for arg in args[1:]]
            qargs = [prefixed_header] + processed_args
        else:
            qargs = [self._rewrite_inline_percents(Validator.quote_if_needed(a), section) for a in args]

        return f"{name}({', '.join(qargs)})" if qargs else f"{name}()"

    def parse_percent_block(self, pct: str) -> tuple[str, str | None]:
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
            tag, payload = inner.split(":", 1)
            return tag, payload
        return inner, None

    def convert_set_to_brackets(self, set_text: str) -> str:
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
        declarations = []
        for (var_type, _), var_name in sorted(self._state_vars.items()):
            declarations.append(f"{var_name}: {var_type.name.lower()};")
        return declarations

    def negate_expression(self, term: str) -> str:
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
        match = Validator._PERCENT_RE.match(percent)
        if not match:
            raise SymbolResolutionError(percent, "Invalid %{...} reference")

        tag = match.group(1)
        payload = match.group(2)

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

        for lhs_key, (commands, _, uppercase, _) in tables.OPERATOR_MAP.items():
            if (isinstance(commands, (list, tuple)) and cmd in commands) or (cmd == commands):
                return self._handle_operator_command(cmd, toks, lhs_key, uppercase, section)

        for name, (forward_cmd, _) in tables.STATEMENT_FUNCTION_MAP.items():
            if forward_cmd == cmd:
                return self._handle_statement_function(name, args, section, op_state)

        raise SymbolResolutionError(line, f"Unknown operator: {cmd}")
