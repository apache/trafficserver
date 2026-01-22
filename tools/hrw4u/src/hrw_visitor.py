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

from functools import lru_cache

from u4wrh.u4wrhVisitor import u4wrhVisitor
from u4wrh.u4wrhParser import u4wrhParser
from .hrw_symbols import InverseSymbolResolver
from hrw4u.errors import SymbolResolutionError
from hrw4u.states import CondState, SectionType
from hrw4u.common import SystemDefaults
from hrw4u.visitor_base import BaseHRWVisitor
from hrw4u.validation import Validator

# Cache regex validator at module level for efficiency
_inverse_regex_validator = Validator.regex_pattern()


class HRWInverseVisitor(u4wrhVisitor, BaseHRWVisitor):
    """Inverse visitor for converting ATS configuration back to HRW4U format."""

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            section_label: SectionType = SectionType.REMAP,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None,
            preserve_comments: bool = True,
            merge_sections: bool = True) -> None:

        super().__init__(filename=filename, debug=debug, error_collector=error_collector)

        # HRW inverse-specific state
        self._section_label = section_label
        self.preserve_comments = preserve_comments
        self.merge_sections = merge_sections
        self._pending_terms: list[tuple[str, CondState]] = []
        self._in_group: bool = False
        self._group_terms: list[tuple[str, CondState]] = []

        self.symbol_resolver = InverseSymbolResolver()

        self._section_opened = False
        self._if_depth = 0  # Track nesting depth of if blocks
        self._in_elif_mode = False
        self._just_closed_nested = False
        self._expecting_if_cond = False  # True after 'if' operator, before its condition

    @lru_cache(maxsize=128)
    def _cached_percent_parsing(self, pct_text: str) -> tuple[str, str | None]:
        """Cache expensive percent block parsing."""
        return self.symbol_resolver.parse_percent_block(pct_text)

    @lru_cache(maxsize=256)
    def _cached_symbol_to_ident(self, pct_text: str, section_name: str) -> tuple[str, str]:
        """Cache expensive symbol resolution operations."""
        try:
            section = SectionType(section_name)
            return self.symbol_resolver.percent_to_ident_or_func(pct_text, section)
        except (ValueError, SymbolResolutionError):
            return pct_text, ""

    #
    # Helpers
    #

    def _reset_condition_state(self) -> None:
        """Reset condition state for new sections."""
        self._pending_terms.clear()
        self._in_elif_mode = False
        self._in_group = False
        self._group_terms.clear()
        self._expecting_if_cond = False

    def _close_if_chain_for_new_rule(self) -> None:
        """Close if-else chain when a new rule starts without elif/else."""
        expecting_nested_if = self._expecting_if_cond
        self._expecting_if_cond = False

        if (self._if_depth > 0 and not self._in_elif_mode and not self._pending_terms and not expecting_nested_if):
            self.debug("new rule detected - closing if chain")
            self._start_new_section(SectionType.REMAP)

    def _start_new_section(self, section_type: SectionType) -> None:
        """Start a new section, handling continuation of existing sections."""
        with self.debug_context(f"start_section {section_type.value}"):
            if self.merge_sections and self._section_opened and self._section_label == section_type:
                self.debug(f"continuing existing section")
                while self._if_depth > 0:
                    self.decrease_indent()
                    self.emit("}")
                    self._if_depth -= 1
                self._reset_condition_state()
                if self.output and self.output[-1] != "":
                    self.output.append("")
                return

            prev = bool(self.output)
            had_section = self._section_opened
            self._close_if_and_section()
            self._reset_condition_state()

            if had_section and self.output and self.output[-1] != "":
                self.output.append("")

            self._section_label = section_type
            self.emit(f"{section_type.value} {{")
            self._section_opened = True
            self.increase_indent()

    def _build_expression_parts(self, terms: list[tuple[str, CondState]]) -> str:
        """Build expression from condition terms."""
        with self.debug_context(f"_build_expression_parts: {terms}"):
            parts: list[str] = []
            connector = "&&"

            for idx, (term, state) in enumerate(terms):
                self.debug(f"term {idx}: {term}, state: {state}")

                if state.not_:
                    processed_term = self.symbol_resolver.negate_expression(term)
                else:
                    processed_term = self._normalize_empty_string_condition(term, state)

                processed_term = self._apply_with_modifiers(processed_term, state)
                self.debug(f"processed term {idx}: {processed_term}")

                if idx > 0:
                    parts.append(connector)
                parts.append(processed_term)

                connector = self._build_condition_connector(state, idx == len(terms) - 1)
                self.debug(f"next connector: {connector}")

            result = " ".join(parts)
            return result

    def _flush_pending_condition(self) -> None:
        """Flush pending condition terms into if/elif statement."""
        if not self._pending_terms:
            return

        expr = self._build_expression_parts(self._pending_terms)
        self._start_if_block(expr)
        self._pending_terms.clear()

    def visitProgram(self, ctx: u4wrhParser.ProgramContext) -> list[str]:
        """Visit program and generate complete HRW4U output."""
        with self.debug_context("visitProgram"):
            for line in ctx.line():
                self.visit(line)
            self._close_if_and_section()

            var_declarations = self.symbol_resolver.get_var_declarations()
            if var_declarations:
                vars_output = '\n'.join(["VARS {", *[self.format_with_indent(decl, 1) for decl in var_declarations], "}", ""])
                self.output = vars_output.split('\n') + self.output

            return self.output

    def visitCommentLine(self, ctx: u4wrhParser.CommentLineContext) -> None:
        """Preserve comments in the output with proper indentation."""
        if not self.preserve_comments:
            return
        with self.debug_context("visitCommentLine"):
            comment_text = ctx.COMMENT().getText()
            self._flush_pending_condition()
            if self._section_opened:
                self.emit(comment_text)
            else:
                self.output.append(comment_text)

    def visitIfLine(self, ctx: u4wrhParser.IfLineContext) -> None:
        """Handle if operator (starts nested conditional)."""
        with self.debug_context("visitIfLine"):
            self._flush_pending_condition()
            self._just_closed_nested = False
            self._expecting_if_cond = True
            return None

    def visitEndifLine(self, ctx: u4wrhParser.EndifLineContext) -> None:
        """Handle endif operator (closes nested conditional)."""
        with self.debug_context("visitEndifLine"):
            self._close_if_block()
            self._just_closed_nested = True
            return None

    def visitElifLine(self, ctx: u4wrhParser.ElifLineContext) -> None:
        """Handle elif line transitions."""
        with self.debug_context("visitElifLine"):
            self._start_elif_mode()
            return None

    def visitElseLine(self, ctx: u4wrhParser.ElseLineContext) -> None:
        """Handle else line transitions."""
        with self.debug_context("visitElseLine"):
            self._handle_else_transition()
            return None

    def visitCondLine(self, ctx: u4wrhParser.CondLineContext) -> None:
        """Process condition lines with error handling."""
        with self.debug_context("visitCondLine"):
            cond_state = CondState()
            if ctx.modList():
                for mod_item in ctx.modList().modItem():
                    cond_state.add_modifier(mod_item.getText())
            self.debug(f"cond_state: {cond_state}")

            body = ctx.condBody()

            pct_text: str | None = None
            if body.bareRef():
                pct_text = body.bareRef().percentRef().getText()
            elif body.functionCond():
                pct_text = body.functionCond().percentFunc().getText()

            match pct_text:
                case str() if pct_text:
                    self.debug(f"percent block: {pct_text}")
                    tag, payload = self._cached_percent_parsing(pct_text)
                    self.debug(f"percent parsed -> tag={tag} payload={payload}")

                    try:
                        section_type = SectionType.from_hook(tag)
                        self.debug("hook => new section: " + section_type.value)
                        self._start_new_section(section_type)
                        return None
                    except ValueError:
                        pass

                    # Not a hook - check if we need to close existing if-else chain
                    self._close_if_chain_for_new_rule()

                    match tag:
                        case "GROUP":
                            if payload is None:
                                self._in_group = True
                                return None
                            elif payload == "END":
                                if self._in_group:
                                    if self._group_terms:
                                        grouped_expr = self._build_expression_parts(self._group_terms)
                                        self._pending_terms.append((f"({grouped_expr})", cond_state))
                                    self._in_group = False
                                    self._group_terms.clear()
                                return None

                        case "TRUE":
                            terms = self._group_terms if self._in_group else self._pending_terms
                            terms.append(("true", cond_state))
                            return None

                        case "FALSE":
                            terms = self._group_terms if self._in_group else self._pending_terms
                            terms.append(("false", cond_state))
                            return None

                        case _:
                            expr = None
                            with self.trap(ctx):
                                expr, _ = self._cached_symbol_to_ident(pct_text, self._section_label.value)
                            if not expr:
                                return None  # skip this term on error
                            terms = self._group_terms if self._in_group else self._pending_terms
                            terms.append((expr, cond_state))
                            return None

                case _:
                    # No percent block - check if we need to close existing if-else chain
                    self._close_if_chain_for_new_rule()

                    if body.comparison():
                        comparison_expr = self._build_comparison_expression(body.comparison())
                        if comparison_expr != "ERROR":  # Skip if error occurred
                            terms = self._group_terms if self._in_group else self._pending_terms
                            terms.append((comparison_expr, cond_state))
                        return None

                    self.handle_error(ValueError("Unrecognized condition body"))
                    return None

    def _build_comparison_expression(self, comparison: u4wrhParser.ComparisonContext) -> str:
        """Build comparison expression with error handling."""
        with self.debug_context("_build_comparison"):
            left_pct = comparison.lhs().getText()
            self.debug(f"LHS raw: '{left_pct}'")

            lhs_expr = None
            with self.trap(comparison):
                lhs_expr, _ = self._cached_symbol_to_ident(left_pct, self._section_label.value)
            if not lhs_expr:
                return "ERROR"

            match comparison:
                case _ if comparison.cmpOp():
                    operator = comparison.cmpOp().getText()
                    rhs = comparison.rhs().value().getText()
                    if operator == "=":
                        operator = "=="
                    result = f"{lhs_expr} {operator} {rhs}"
                    self.debug(f"comparison -> {result}")
                    return result

                case _ if comparison.regex():
                    regex_expr = comparison.regex().getText()
                    try:
                        _inverse_regex_validator(regex_expr)
                    except Exception as e:
                        with self.trap(comparison.regex()):
                            raise e
                        return "ERROR"
                    result = f"{lhs_expr} ~ {regex_expr}"
                    self.debug(f"comparison -> {result}")
                    return result

                case _ if (set_ctx := comparison.set_()):
                    set_text = self.symbol_resolver.convert_set_to_brackets(set_ctx.getText())
                    result = f"{lhs_expr} in {set_text}"
                    self.debug(f"comparison -> {result}")
                    return result

                case _ if (iprange_ctx := comparison.iprange()):
                    iprange_text = self.symbol_resolver.format_iprange(iprange_ctx.getText())
                    result = f"{lhs_expr} in {iprange_text}"
                    self.debug(f"comparison -> {result}")
                    return result

                case _ if comparison.STRING():
                    string_value = comparison.STRING().getText()
                    result = f"{lhs_expr} == {string_value}"
                    self.debug(f"implicit string comparison -> {result}")
                    return result

                case _ if comparison.NUMBER():
                    number_value = comparison.NUMBER().getText()
                    result = f"{lhs_expr} == {number_value}"
                    self.debug(f"implicit number comparison -> {result}")
                    return result

                case _ if comparison.IDENT():
                    ident_value = comparison.IDENT().getText()
                    result = f"{lhs_expr} == {ident_value}"
                    self.debug(f"implicit ident comparison -> {result}")
                    return result

                case _ if comparison.COMPLEX_STRING():
                    complex_value = comparison.COMPLEX_STRING().getText()
                    result = f"{lhs_expr} == {complex_value}"
                    self.debug(f"implicit complex string comparison -> {result}")
                    return result

                case _:
                    with self.trap(comparison):
                        raise ValueError("Invalid comparison")
                    return "ERROR"

    def visitOpLine(self, ctx: u4wrhParser.OpLineContext) -> None:
        """Process operation lines with comprehensive error handling."""
        with self.debug_context("visitOpLine"):
            self._ensure_section_open(self._section_label)
            self._flush_pending_condition()

            node = ctx.opText()
            cmd = node.IDENT().getText() if node.IDENT() else None
            args, op_state, _cond_state = self._parse_op_tails(node, ctx)

            self.debug(f"operator: {cmd} args={args} op_state={op_state} cond_state={_cond_state}")
            if cmd == "set-redirect":
                args = self._reconstruct_redirect_args(args)
                self.debug(f"reconstructed redirect: {args}")

            stmt = self.symbol_resolver.op_to_hrw4u(cmd, args, self._section_label, op_state)
            self.emit(stmt + ";")

            return None

    # Condition block lifecycle methods - specific to inverse visitor
    def _close_if_block(self) -> None:
        """Close open if block."""
        if self._if_depth > 0:
            self.decrease_indent()
            self.emit("}")
            self._if_depth -= 1

    def _close_section(self) -> None:
        """Close open section."""
        if self._section_opened:
            self.decrease_indent()
            self.emit("}")
            self._section_opened = False

    def _close_if_and_section(self) -> None:
        """Close open if blocks and sections."""
        while self._if_depth > 0:
            self._close_if_block()
        self._close_section()
        self._in_elif_mode = False

    def _ensure_section_open(self, section_label: SectionType) -> None:
        """Ensure a section is open for statements."""
        if not self._section_opened:
            self.emit(f"{section_label.value} {{")
            self._section_opened = True
            self.increase_indent()

    def _start_elif_mode(self) -> None:
        """Handle elif line transitions."""
        # After endif, we need to close the parent if-statement
        if self._if_depth > 0:
            self.decrease_indent()
            self._if_depth -= 1
        self._in_elif_mode = True
        self._just_closed_nested = False

    def _handle_else_transition(self) -> None:
        """Handle else line transitions."""
        if self._if_depth > 0:
            self.decrease_indent()
            self._if_depth -= 1
        self.emit("} else {")
        self._if_depth += 1
        self.increase_indent()
        self._just_closed_nested = False

    def _start_if_block(self, condition_expr: str) -> None:
        """Start a new if block."""
        if self._in_elif_mode:
            self.emit(f"}} elif {condition_expr} {{")
            self._in_elif_mode = False
        else:
            self.emit(f"if {condition_expr} {{")

        self._if_depth += 1
        self.increase_indent()
