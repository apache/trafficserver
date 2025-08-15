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

from u4wrh.u4wrhVisitor import u4wrhVisitor
from u4wrh.u4wrhParser import u4wrhParser
from .hrw_symbols import InverseSymbolResolver
from hrw4u.errors import hrw4u_error, SymbolResolutionError
from hrw4u.validation import Validator
from hrw4u.debugging import Dbg
from hrw4u.states import CondState, OperatorState, SectionType, ModifierType
from hrw4u.common import VisitorMixin, SystemDefaults


class HRWInverseVisitor(u4wrhVisitor, VisitorMixin):

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            section_label: SectionType = SectionType.REMAP,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None) -> None:
        self.filename = filename
        self.section_label = section_label
        self.output: list[str] = []
        self.error_collector = error_collector

        self._pending_terms: list[tuple[str, CondState]] = []
        self._in_group: bool = False
        self._group_terms: list[tuple[str, CondState]] = []

        self._in_if_block = False
        self._section_opened = False
        self._stmt_indent = 0
        self._in_elif_mode = False

        self._dbg = Dbg(debug)

        self.symbol_resolver = InverseSymbolResolver()

#
# Helpers
#

    def _emit(self, s: str) -> None:
        self.output.append(self.format_with_indent(s, self._stmt_indent))

    def _close_if_and_section(self) -> None:
        if self._in_if_block:
            self._stmt_indent -= 1
            self._emit("}")
            self._in_if_block = False
        if self._section_opened:
            self._stmt_indent -= 1
            self._emit("}")
            self._section_opened = False
        self._in_elif_mode = False

    def _reset_condition_state(self) -> None:
        self._pending_terms.clear()
        self._in_elif_mode = False
        self._in_group = False
        self._group_terms.clear()

    def _start_new_section(self, section_type: SectionType) -> None:
        self._dbg.enter(f"start_section {section_type.value}")

        if self._section_opened and self.section_label == section_type:
            self._dbg("continuing existing section")
            if self._in_if_block:
                self._stmt_indent -= 1
                self._emit("}")
                self._in_if_block = False
            self._reset_condition_state()
            if self.output and self.output[-1] != "":
                self.output.append("")
            self._dbg.exit(f"continue_section {section_type.value}")
            return

        prev = bool(self.output)
        self._close_if_and_section()
        self._reset_condition_state()

        if prev and (not self.output or self.output[-1] != ""):
            self.output.append("")

        self.section_label = section_type
        self._emit(f"{section_type.value} {{")
        self._section_opened = True
        self._stmt_indent += 1
        self._dbg.exit(f"start_section {section_type.value}")

    def _ensure_section(self) -> None:
        if not self._section_opened:
            self._emit(f"{self.section_label.value} {{")
            self._section_opened = True
            self._stmt_indent += 1

    def _apply_with_modifiers(self, expr: str, state: CondState) -> str:
        with_mods = state.to_with_modifiers()
        return f"{expr} with {','.join(with_mods)}" if with_mods else expr

    def _build_expression_parts(self, terms: list[tuple[str, CondState]]) -> str:
        self._dbg.enter(f"_build_expression_parts: {terms}")
        parts: list[str] = []
        connector = "&&"

        for idx, (term, state) in enumerate(terms):
            self._dbg(f"term {idx}: {term}, state: {state}")

            if state.not_:
                processed_term = self.symbol_resolver.negate_expression(term)
            else:
                if ' != ""' in term:
                    processed_term = term.replace(' != ""', '')
                elif ' == ""' in term:
                    processed_term = f"!{term.replace(' == \"\"', '')}"
                else:
                    processed_term = term

            processed_term = self._apply_with_modifiers(processed_term, state)
            self._dbg(f"processed term {idx}: {processed_term}")

            if idx > 0:
                parts.append(connector)
            parts.append(processed_term)

            if state.and_or:
                connector = "||"
            else:
                connector = "&&"
            self._dbg(f"next connector: {connector}")

        result = " ".join(parts)
        self._dbg.exit(f"result: {result}")
        return result

    def _flush_pending_condition(self) -> None:
        if not self._pending_terms:
            return

        expr = self._build_expression_parts(self._pending_terms)

        if self._in_elif_mode:
            self._emit(f"}} elif {expr} {{")
            self._in_elif_mode = False
        else:
            self._emit(f"if {expr} {{")

        self._in_if_block = True
        self._stmt_indent += 1
        self._pending_terms.clear()


#
# Visitors
#

    def visitProgram(self, ctx: u4wrhParser.ProgramContext) -> list[str]:
        self._dbg.enter("visitProgram")
        try:
            for line in ctx.line():
                self.visit(line)
            self._close_if_and_section()

            var_declarations = self.symbol_resolver.get_var_declarations()
            if var_declarations:
                vars_section = ["VARS {"]
                for decl in var_declarations:
                    vars_section.append(self.format_with_indent(decl, 1))
                vars_section.extend(["}", ""])
                self.output = vars_section + self.output

            return self.output
        finally:
            self._dbg.exit("visitProgram")

    def visitElifLine(self, ctx: u4wrhParser.ElifLineContext) -> None:
        self._dbg.enter("visitElifLine")
        try:
            if self._in_if_block:
                self._stmt_indent -= 1
                self._in_if_block = False
            self._in_elif_mode = True
            return None
        finally:
            self._dbg.exit("visitElifLine")

    def visitElseLine(self, ctx: u4wrhParser.ElseLineContext) -> None:
        self._dbg.enter("visitElseLine")
        try:
            if self._in_if_block:
                self._stmt_indent -= 1

                if self.output and self.output[-1].strip() == "}":
                    self.output[-1] = self.format_with_indent("} else {", self._stmt_indent)
                else:
                    self._emit("} else {")

                self._in_if_block = True
                self._stmt_indent += 1
                return None

            self._emit("else {")
            self._stmt_indent += 1
            return None
        finally:
            self._dbg.exit("visitElseLine")

    def visitCondLine(self, ctx: u4wrhParser.CondLineContext) -> None:
        self._dbg.enter("visitCondLine")
        try:
            cond_state = CondState()
            if ctx.modList():
                for mod_item in ctx.modList().modItem():
                    cond_state.add_modifier(mod_item.getText())
            self._dbg(f"cond_state: {cond_state}")

            body = ctx.condBody()

            pct_text: str | None = None
            if body.bareRef():
                pct_text = body.bareRef().percentRef().getText()
            elif body.functionCond():
                pct_text = body.functionCond().percentFunc().getText()

            match pct_text:
                case str() if pct_text:
                    self._dbg(f"percent block: {pct_text}")
                    tag, payload = self.symbol_resolver.parse_percent_block(pct_text)
                    self._dbg(f"percent parsed -> tag={tag} payload={payload}")

                    try:
                        section_type = SectionType.from_hook(tag)
                        self._dbg("hook => new section: " + section_type.value)
                        self._start_new_section(section_type)
                        return None
                    except ValueError:
                        pass

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
                            try:
                                expr, _ = self.symbol_resolver.percent_to_ident_or_func(pct_text, self.section_label)
                            except SymbolResolutionError as exc:
                                error = hrw4u_error(self.filename, ctx, exc)
                                if self.error_collector:
                                    self.error_collector.add_error(error)
                                    return None  # Continue processing
                                else:
                                    raise error
                            terms = self._group_terms if self._in_group else self._pending_terms
                            terms.append((expr, cond_state))
                            return None

                case _:
                    if body.comparison():
                        comparison_expr = self._build_comparison_expression(body.comparison())
                        terms = self._group_terms if self._in_group else self._pending_terms
                        terms.append((comparison_expr, cond_state))
                        return None

                    error = hrw4u_error(self.filename, body, "Unrecognized condition body")
                    if self.error_collector:
                        self.error_collector.add_error(error)
                        return None  # Continue processing
                    else:
                        raise error
        finally:
            self._dbg.exit("visitCondLine")

    def _build_comparison_expression(self, comparison: u4wrhParser.ComparisonContext) -> str:
        self._dbg.enter("_build_comparison")
        try:
            left_pct = comparison.lhs().getText()
            self._dbg(f"LHS raw: '{left_pct}'")
            try:
                lhs_expr, _ = self.symbol_resolver.percent_to_ident_or_func(left_pct, self.section_label)
            except SymbolResolutionError as exc:
                error = hrw4u_error(self.filename, comparison, exc)
                if self.error_collector:
                    self.error_collector.add_error(error)
                    return "ERROR"  # Return placeholder to continue processing
                else:
                    raise error

            match comparison:
                case _ if comparison.cmpOp():
                    operator = comparison.cmpOp().getText()
                    rhs = comparison.rhs().value().getText()
                    if operator == "=":
                        operator = "=="
                    result = f"{lhs_expr} {operator} {rhs}"
                    self._dbg(f"comparison -> {result}")
                    return result

                case _ if comparison.regexOp():
                    regex_op = comparison.regexOp().getText()
                    regex_expr = comparison.regex().getText()
                    result = f"{lhs_expr} {regex_op} {regex_expr}"
                    self._dbg(f"comparison -> {result}")
                    return result

                case _ if comparison.regex():
                    regex_expr = comparison.regex().getText()
                    result = f"{lhs_expr} ~ {regex_expr}"
                    self._dbg(f"comparison -> {result}")
                    return result

                case _ if comparison.inOp():
                    if comparison.set_():
                        set_text = self.symbol_resolver.convert_set_to_brackets(comparison.set_().getText())
                        result = f"{lhs_expr} in {set_text}"
                        self._dbg(f"comparison -> {result}")
                        return result
                    if comparison.iprange():
                        iprange_text = self.symbol_resolver.format_iprange(comparison.iprange().getText())
                        result = f"{lhs_expr} in {iprange_text}"
                        self._dbg(f"comparison -> {result}")
                        return result

                case _ if (set_ctx := comparison.set_()):
                    set_text = self.symbol_resolver.convert_set_to_brackets(set_ctx.getText())
                    result = f"{lhs_expr} in {set_text}"
                    self._dbg(f"comparison -> {result}")
                    return result

                case _ if (iprange_ctx := comparison.iprange()):
                    iprange_text = self.symbol_resolver.format_iprange(iprange_ctx.getText())
                    result = f"{lhs_expr} in {iprange_text}"
                    self._dbg(f"comparison -> {result}")
                    return result

                case _:
                    error = hrw4u_error(self.filename, comparison, "Invalid comparison")
                    if self.error_collector:
                        self.error_collector.add_error(error)
                        return "ERROR"  # Return placeholder to continue processing
                    else:
                        raise error
        finally:
            self._dbg.exit("_build_comparison")

    def visitOpLine(self, ctx: u4wrhParser.OpLineContext) -> None:
        self._dbg.enter("visitOpLine")
        try:
            self._ensure_section()
            self._flush_pending_condition()

            node = ctx.opText()
            cmd: str | None = None
            args: list[str] = []
            cond_state = CondState()
            op_state = OperatorState()

            if node.IDENT():
                cmd = node.IDENT().getText()

            for tail in node.opTail():
                if getattr(tail, "LBRACKET", None) and tail.LBRACKET():
                    for flag in tail.opFlag():
                        flag_text = flag.getText().upper()
                        mod_type = ModifierType.classify(flag_text)
                        if mod_type == ModifierType.CONDITION:
                            cond_state.add_modifier(flag_text)
                        elif mod_type == ModifierType.OPERATOR:
                            op_state.add_modifier(flag_text)
                        else:
                            raise Exception(f"Unknown modifier: {flag_text}")
                    continue
                if getattr(tail, "IDENT", None) and tail.IDENT():
                    args.append(tail.IDENT().getText())
                    continue
                if getattr(tail, "NUMBER", None) and tail.NUMBER():
                    args.append(tail.NUMBER().getText())
                    continue
                if getattr(tail, "STRING", None) and tail.STRING():
                    args.append(tail.STRING().getText())
                    continue
                if getattr(tail, "PERCENT_BLOCK", None) and tail.PERCENT_BLOCK():
                    args.append(tail.PERCENT_BLOCK().getText())
                    continue
                if getattr(tail, "COMPLEX_STRING", None) and tail.COMPLEX_STRING():
                    args.append(tail.COMPLEX_STRING().getText())
                    continue
                tail_text = tail.getText()
                if tail_text:
                    args.append(tail_text)

            self._dbg(f"operator: {cmd} args={args} op_state={op_state} cond_state={cond_state}")
            if cmd == "set-redirect" and len(args) > 1:
                url_parts = []
                for arg in args[1:]:
                    if arg.startswith('"') and arg.endswith('"'):
                        url_parts.append(arg[1:-1])
                    else:
                        url_parts.append(arg)
                reconstructed_url = "".join(url_parts)
                args = [args[0], reconstructed_url]
                self._dbg(f"reconstructed redirect: {args}")

            stmt = self.symbol_resolver.op_to_hrw4u(cmd, args, self.section_label, op_state)
            self._emit(stmt + ";")

            return None
        except Exception as exc:
            error = hrw4u_error(self.filename, ctx, exc)
            if self.error_collector:
                self.error_collector.add_error(error)
                return None  # Continue processing other operations
            else:
                raise error
        finally:
            self._dbg.exit("visitOpLine")
