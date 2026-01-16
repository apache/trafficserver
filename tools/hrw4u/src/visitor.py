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
from dataclasses import dataclass
from functools import lru_cache

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.symbols import SymbolResolver, SymbolResolutionError
from hrw4u.errors import hrw4u_error
from hrw4u.states import CondState, SectionType
from hrw4u.common import RegexPatterns, SystemDefaults
from hrw4u.visitor_base import BaseHRWVisitor
from hrw4u.validation import Validator

# Cache regex validator at module level for efficiency
_regex_validator = Validator.regex_pattern()


@dataclass(slots=True)
class QueuedItem:
    text: str
    state: CondState
    indent: int


class HRW4UVisitor(hrw4uVisitor, BaseHRWVisitor):
    _SUBSTITUTE_PATTERN = RegexPatterns.SUBSTITUTE_PATTERN

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None,
            preserve_comments: bool = True) -> None:
        super().__init__(filename, debug, error_collector)

        self._cond_state = CondState()
        self._queued: QueuedItem | None = None
        self.preserve_comments = preserve_comments

        self.symbol_resolver = SymbolResolver(debug)

    @lru_cache(maxsize=256)
    def _cached_symbol_resolution(self, symbol_text: str, section_name: str) -> tuple[str, bool]:
        """Cache expensive symbol resolution operations."""
        try:
            section = SectionType(section_name)
            return self.symbol_resolver.resolve_condition(symbol_text, section)
        except (ValueError, SymbolResolutionError):
            return symbol_text, False

    @lru_cache(maxsize=128)
    def _cached_hook_mapping(self, section_name: str) -> str:
        """Cache hook mapping lookups."""
        return self.symbol_resolver.map_hook(section_name)

    def _make_condition(self, cond_text: str, last: bool = False, negate: bool = False) -> str:
        self._dbg(f"make_condition: {cond_text} last={last} negate={negate}")
        self._cond_state.not_ |= negate
        self._cond_state.last = last
        return f"cond {cond_text}"

    def _queue_condition(self, text: str) -> None:
        self.debug_log(f"queue cond: {text}  state={self._cond_state.to_list()}")
        self._queued = QueuedItem(text=text, state=self._cond_state.copy(), indent=self.cond_indent)
        self._cond_state.reset()

    def _flush_condition(self) -> None:
        """
        Flush any queued condition to output.
        """
        if self._queued:
            mods = self._queued.state.to_list()
            self.debug_log(f"flush cond: {self._queued.text} state={mods} indent={self._queued.indent}")
            mod_suffix = self._queued.state.render_suffix()
            self.output.append(self.format_with_indent(f"{self._queued.text}{mod_suffix}", self._queued.indent))
            self._queued = None

    def _parse_function_call(self, ctx) -> tuple[str, list[str]]:
        if ctx.funcName is None:
            raise SymbolResolutionError("function", "Missing function name")
        func = ctx.funcName.text
        args = [v.getText() for v in ctx.argumentList().value()] if ctx.argumentList() else []
        return func, args

    def _parse_function_args(self, arg_str: str) -> list[str]:
        """
        Parse function arguments correctly handling quotes and nested parentheses.
        """
        if not arg_str.strip():
            return []

        args = []
        current_arg = []
        paren_depth = 0
        in_quotes = False
        quote_char = None
        i = 0

        while i < len(arg_str):
            char = arg_str[i]

            if not in_quotes:
                if char in ('"', "'"):
                    in_quotes = True
                    quote_char = char
                    current_arg.append(char)
                elif char == '(':
                    paren_depth += 1
                    current_arg.append(char)
                elif char == ')':
                    paren_depth -= 1
                    current_arg.append(char)
                elif char == ',' and paren_depth == 0:
                    args.append(''.join(current_arg).strip())
                    current_arg = []
                else:
                    current_arg.append(char)
            else:
                current_arg.append(char)
                if char == quote_char:
                    if i == 0 or arg_str[i - 1] != '\\':
                        in_quotes = False
                        quote_char = None
            i += 1

        if current_arg:
            args.append(''.join(current_arg).strip())

        return args

    def _substitute_strings(self, s: str, ctx) -> str:
        """Optimized string substitution using string builder."""
        inner = s[1:-1]

        def repl(m: re.Match) -> str:
            try:
                if m.group("func"):
                    func_name = m.group("func").strip()
                    arg_str = m.group("args").strip()
                    args = self._parse_function_args(arg_str) if arg_str else []
                    replacement = self.symbol_resolver.resolve_function(func_name, args, strip_quotes=False)
                    self.debug_log(f"substitute: {{{func_name}({arg_str})}} -> {replacement}")
                    return replacement
                if m.group("var"):
                    var_name = m.group("var").strip()
                    # Use resolve_condition directly to properly validate section restrictions
                    replacement, _ = self.symbol_resolver.resolve_condition(var_name, self.current_section)
                    self.debug_log(f"substitute: {{{var_name}}} -> {replacement}")
                    return replacement
                raise SymbolResolutionError(m.group(0), "Unrecognized substitution format")
            except Exception as e:
                error = hrw4u_error(self.filename, ctx, f"symbol error in {{}}: {e}")
                if hasattr(error, 'add_note'):
                    error.add_note(f"String interpolation context: {s[:50]}...")
                    if self.current_section:
                        error.add_note(f"Current section: {self.current_section.value}")
                if self.error_collector:
                    self.error_collector.add_error(error)
                    return f"{{ERROR: {e}}}"
                else:
                    raise error

        substituted = self._SUBSTITUTE_PATTERN.sub(repl, inner)
        return f'"{substituted}"'

    def _resolve_identifier_with_validation(self, name: str) -> tuple[str, bool]:
        """
        Resolve an identifier with proper validation for declared variables vs system fields.
        """
        if not name:
            raise SymbolResolutionError("identifier", "Missing or empty identifier text")

        if entry := self.symbol_resolver.symbol_for(name):
            return entry.as_cond(), False

        symbol, default_expr = self._cached_symbol_resolution(name, self.current_section.value)

        # If resolution failed (symbol == name), we need to validate
        if symbol == name:
            if '.' not in name and ':' not in name:
                error = SymbolResolutionError(
                    "identifier", f"Undefined variable: '{name}'. Variables must be declared in a VARS section.")
                suggestions = self.symbol_resolver.get_variable_suggestions(name, self.current_section)
                if suggestions:
                    error.add_symbol_suggestion(suggestions)
                raise error
            else:
                try:
                    return self.symbol_resolver.resolve_condition(name, self.current_section)
                except SymbolResolutionError:
                    raise

        return symbol, default_expr


#
# Visitor Methods
#

    def visitProgram(self, ctx) -> list[str]:
        with self.debug_context("visitProgram"):
            program_items = ctx.programItem()
            for idx, item in enumerate(program_items):
                start_length = len(self.output)
                if item.section():
                    self.visit(item.section())
                    if idx < len(program_items) - 1 and len(self.output) > start_length:
                        next_items = program_items[idx + 1:]
                        if any(next_item.section() for next_item in next_items):
                            self.emit_separator()
                elif item.commentLine() and self.preserve_comments:
                    comment_text = item.commentLine().COMMENT().getText()
                    self.output.append(comment_text)
            self._flush_condition()
            return self.get_final_output()

    def visitSection(self, ctx) -> None:
        with self.debug_context("visitSection"):
            if ctx.varSection():
                return self.visitVarSection(ctx.varSection())

            hook = None
            with self.trap(ctx):
                hook = self._prepare_section(ctx)
            if hook:
                self._emit_section_body(ctx.sectionBody(), hook)

    def _prepare_section(self, ctx):
        """Extract section info and validate. Returns hook name."""
        if ctx.name is None:
            raise SymbolResolutionError("section", "Missing section name")

        section_name = ctx.name.text
        try:
            self.current_section = SectionType(section_name)
        except ValueError:
            valid_sections = [s.value for s in SectionType]
            raise ValueError(f"Invalid section name: '{section_name}'. Valid sections: {', '.join(valid_sections)}")

        hook = self._cached_hook_mapping(section_name)
        self.debug_log(f"`{section_name}' -> `{hook}'")
        return hook

    def _emit_section_header(self, hook, pending_comments):
        """Emit the section hook condition and flush any pending comments."""
        self.emit_condition(f"cond %{{{hook}}} [AND]", final=True)
        for comment in pending_comments:
            self.visit(comment)

    def _emit_section_body(self, section_bodies, hook):
        """Process section body maintaining original hook emission behavior."""
        in_statement_block = False
        first_hook_emitted = False
        pending_leading_comments = []

        for idx, body in enumerate(section_bodies):
            is_conditional = body.conditional() is not None
            is_comment = body.commentLine() is not None

            if is_comment:
                if self.preserve_comments:
                    if not first_hook_emitted:
                        pending_leading_comments.append(body)
                    else:
                        self.visit(body)
            elif is_conditional or not in_statement_block:
                if idx > 0:
                    self._flush_condition()
                    self.output.append("")

                self._emit_section_header(hook, [])
                if not first_hook_emitted:
                    first_hook_emitted = True
                    for comment in pending_leading_comments:
                        self.visit(comment)
                    pending_leading_comments = []

                if is_conditional:
                    self.visit(body)
                    in_statement_block = False
                else:
                    in_statement_block = True
                    with self.stmt_indented():
                        self.visit(body)
            else:
                with self.stmt_indented():
                    self.visit(body)

        # Handle case where section has only comments
        if not first_hook_emitted and pending_leading_comments:
            self._emit_section_header(hook, pending_leading_comments)

    def visitVarSection(self, ctx) -> None:
        if self.current_section is not None:
            error = hrw4u_error(self.filename, ctx, "Variable section must be first in a section")
            if self.error_collector:
                self.error_collector.add_error(error)
                return
            else:
                raise error
        with self.debug_context("visitVarSection"):
            self.visit(ctx.variables())

    def visitCommentLine(self, ctx) -> None:
        if not self.preserve_comments:
            return
        with self.debug_context("visitCommentLine"):
            comment_text = ctx.COMMENT().getText()
            self.debug_log(f"preserving comment: {comment_text}")
            self.output.append(comment_text)

    def visitStatement(self, ctx) -> None:
        with self.debug_context("visitStatement"), self.trap(ctx):
            match ctx:
                case _ if ctx.BREAK():
                    self._dbg("BREAK")
                    self.emit_statement("no-op [L]")
                    return

                case _ if ctx.functionCall():
                    func, args = self._parse_function_call(ctx.functionCall())
                    subst_args = [
                        self._substitute_strings(arg, ctx) if arg.startswith('"') and arg.endswith('"') else arg for arg in args
                    ]
                    symbol = self.symbol_resolver.resolve_statement_func(func, subst_args, self.current_section)
                    self.emit_statement(symbol)
                    return

                case _ if ctx.EQUAL():
                    if ctx.lhs is None:
                        raise SymbolResolutionError("assignment", "Missing left-hand side in assignment")
                    lhs = ctx.lhs.text
                    rhs = ctx.value().getText()
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    self._dbg(f"assignment: {lhs} = {rhs}")
                    out = self.symbol_resolver.resolve_assignment(lhs, rhs, self.current_section)
                    self.emit_statement(out)
                    return

                case _ if ctx.PLUSEQUAL():
                    if ctx.lhs is None:
                        raise SymbolResolutionError("assignment", "Missing left-hand side in += assignment")
                    lhs = ctx.lhs.text
                    rhs = ctx.value().getText()
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    self._dbg(f"add assignment: {lhs} += {rhs}")
                    out = self.symbol_resolver.resolve_add_assignment(lhs, rhs, self.current_section)
                    self.emit_statement(out)
                    return

                case _:
                    if ctx.op is None:
                        raise SymbolResolutionError("operator", "Missing operator in statement")
                    operator = ctx.op.text
                    self._dbg(f"standalone op: {operator}")
                    cmd, validator = self.symbol_resolver.get_statement_spec(operator)
                    if validator:
                        raise SymbolResolutionError(operator, "This operator requires an argument")
                    self.emit_statement(cmd)
                    return

    def visitVariables(self, ctx) -> None:
        with self.debug_context("visitVariables"):
            for item in ctx.variablesItem():
                self.visit(item)

    def visitVariablesItem(self, ctx) -> None:
        with self.debug_context("visitVariablesItem"):
            if ctx.variableDecl():
                self.visit(ctx.variableDecl())
            elif ctx.commentLine() and self.preserve_comments:
                self.visit(ctx.commentLine())

    def visitVariableDecl(self, ctx) -> None:
        with self.debug_context("visitVariableDecl"):
            try:
                if ctx.name is None:
                    raise SymbolResolutionError("variable", "Missing variable name in declaration")
                if ctx.typeName is None:
                    raise SymbolResolutionError("variable", "Missing type name in declaration")
                name = ctx.name.text
                type_name = ctx.typeName.text
                explicit_slot = int(ctx.slot.text) if ctx.slot else None

                if '.' in name or ':' in name:
                    raise SymbolResolutionError("variable", f"Variable name '{name}' cannot contain '.' or ':' characters")

                symbol = self.symbol_resolver.declare_variable(name, type_name, explicit_slot)
                slot_info = f" @{explicit_slot}" if explicit_slot is not None else ""
                self._dbg(f"bind `{name}' to {symbol}{slot_info}")
            except Exception as e:
                name = getattr(ctx, 'name', None)
                type_name = getattr(ctx, 'typeName', None)
                slot = getattr(ctx, 'slot', None)
                note = f"Variable declaration: {name.text}:{type_name.text}" + \
                    (f" @{slot.text}" if slot else "") if name and type_name else None
                with self.trap(ctx, note=note):
                    raise e
                return

    def visitConditional(self, ctx) -> None:
        with self.debug_context("visitConditional"):
            self.visit(ctx.ifStatement())
            for elif_ctx in ctx.elifClause():
                self.visit(elif_ctx)
            if ctx.elseClause():
                self.visit(ctx.elseClause())

    def visitIfStatement(self, ctx) -> None:
        with self.debug_context("visitIfStatement"):
            self.visit(ctx.condition())
            self.visit(ctx.block())

    def visitElseClause(self, ctx) -> None:
        with self.debug_context("visitElseClause"):
            self.emit_condition("else", final=True)
            self.visit(ctx.block())

    def visitElifClause(self, ctx) -> None:
        with self.debug_context("visitElifClause"):
            self.emit_condition("elif", final=True)
            with self.stmt_indented(), self.cond_indented():
                self.visit(ctx.condition())
                self.visit(ctx.block())

    def visitBlock(self, ctx) -> None:
        with self.debug_context("visitBlock"):
            with self.stmt_indented():
                for item in ctx.blockItem():
                    if item.statement():
                        self.visit(item.statement())
                    elif item.conditional():
                        # Nested conditional - emit if/endif operators with saved state
                        self.emit_statement("if")
                        saved_indents = self.stmt_indent, self.cond_indent
                        self.stmt_indent += 1
                        self.cond_indent = self.stmt_indent
                        self.visit(item.conditional())
                        self.stmt_indent, self.cond_indent = saved_indents
                        self.emit_statement("endif")
                    elif item.commentLine() and self.preserve_comments:
                        self.visit(item.commentLine())

    def visitCondition(self, ctx) -> None:
        with self.debug_context("visitCondition"):
            self.emit_expression(ctx.expression(), last=True)
            self._flush_condition()

    def visitComparison(self, ctx, *, last: bool = False) -> None:
        with self.debug_context("visitComparison"):
            comp = ctx.comparable()
            lhs = None
            with self.trap(ctx):
                if comp.ident:
                    ident_name = comp.ident.text
                    lhs, _ = self._resolve_identifier_with_validation(ident_name)
                else:
                    lhs = self.visitFunctionCall(comp.functionCall())
            if not lhs:
                return
            operator = ctx.getChild(1)
            negate = operator.symbol.type in (hrw4uParser.NEQ, hrw4uParser.NOT_TILDE)

            match ctx:
                case _ if ctx.value():
                    rhs = ctx.value().getText()
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    match operator.symbol.type:
                        case hrw4uParser.EQUALS | hrw4uParser.NEQ:
                            cond_txt = f"{lhs} ={rhs}"
                        case _:
                            cond_txt = f"{lhs} {operator.getText()}{rhs}"

                case _ if ctx.regex():
                    regex_expr = ctx.regex().getText()
                    try:
                        _regex_validator(regex_expr)
                    except Exception as e:
                        with self.trap(ctx.regex()):
                            raise e
                        regex_expr = "/.*/'"  # return "ERROR" is for error_collector case only
                    cond_txt = f"{lhs} {regex_expr}"

                # IP Ranges are a bit special, we keep the {} verbatim and no quotes allowed
                case _ if ctx.iprange():
                    cond_txt = f"{lhs} {ctx.iprange().getText()}"

                case _ if ctx.set_():
                    inner = ctx.set_().getText()[1:-1]
                    # We no longer strip the quotes here for sets, fixed in #12256
                    cond_txt = f"{lhs} ({inner})"

                case _:
                    raise hrw4u_error(self.filename, ctx, "Invalid comparison (should not happen)")

            if ctx.modifier():
                self.visit(ctx.modifier())
            self._dbg(f"comparison: {cond_txt}")
            cond = self._make_condition(cond_txt, last=last, negate=negate)
            self.emit_condition(cond)

    def visitModifier(self, ctx) -> None:
        for token in ctx.modifierList().mods:
            try:
                mod = token.text.upper()
                self._cond_state.add_modifier(mod)
            except Exception as exc:
                with self.trap(ctx):
                    raise exc
                return

    def visitFunctionCall(self, ctx) -> str:
        with self.trap(ctx):
            func, raw_args = self._parse_function_call(ctx)
            self._dbg(f"function: {func}({', '.join(raw_args)})")
            return self.symbol_resolver.resolve_function(func, raw_args, strip_quotes=True)
        return "ERROR"

    def emit_condition(self, text: str, *, final: bool = False) -> None:
        if final:
            self.output.append(self.format_with_indent(text, self.cond_indent))
        else:
            if self._queued:
                self._flush_condition()
            self._queue_condition(text)

    def emit_separator(self) -> None:
        """Emit a blank line separator."""
        self.output.append("")

    def emit_statement(self, line: str) -> None:
        """Override base class method to handle condition flushing."""
        self._flush_condition()
        super().emit_statement(line)

    def _end_lhs_then_emit_rhs(self, set_and_or: bool, rhs_emitter) -> None:
        """
        Helper for expression emission: update queued state, flush, then emit RHS.
        """
        if self._queued:
            self._queued.state.and_or = set_and_or
            if not set_and_or:
                self._queued.indent = self.cond_indent
        self._flush_condition()
        rhs_emitter()

    def emit_expression(self, ctx, *, nested: bool = False, last: bool = False, grouped: bool = False) -> None:
        with self.debug_context("emit_expression"):
            if ctx.OR():
                self.debug_log("`OR' detected")
                if grouped:
                    self.debug_log("GROUP-START")
                    self.emit_condition("cond %{GROUP}", final=True)
                    with self.cond_indented():
                        self.emit_expression(ctx.expression(), nested=False, last=False)
                        self._end_lhs_then_emit_rhs(True, lambda: self.emit_term(ctx.term(), last=last))
                    self.emit_condition("cond %{GROUP:END}")
                else:
                    self.emit_expression(ctx.expression(), nested=False, last=False)
                    self._end_lhs_then_emit_rhs(True, lambda: self.emit_term(ctx.term(), last=last))
            else:
                self.emit_term(ctx.term(), last=last)

    def emit_term(self, ctx, *, last: bool = False) -> None:
        with self.debug_context("emit_term"):
            if ctx.AND():
                self.debug_log("`AND' detected")
                self.emit_term(ctx.term(), last=False)
                self._end_lhs_then_emit_rhs(False, lambda: self.emit_factor(ctx.factor(), last=last))
            else:
                self.emit_factor(ctx.factor(), last=last)

    def emit_factor(self, ctx, *, last: bool = False) -> None:
        with self.debug_context("emit_factor"), self.trap(ctx):
            match ctx:
                case _ if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
                    self._dbg("`NOT' detected")
                    self._cond_state.not_ = True
                    self.emit_factor(ctx.getChild(1), last=last)

                case _ if ctx.LPAREN():
                    self._dbg("GROUP-START")
                    self.emit_condition("cond %{GROUP}", final=True)
                    with self.cond_indented():
                        self.emit_expression(ctx.expression(), nested=False, last=True, grouped=True)
                    self._cond_state.last = last
                    self.emit_condition("cond %{GROUP:END}")

                case _ if ctx.comparison():
                    self.visitComparison(ctx.comparison(), last=last)

                case _ if ctx.functionCall():
                    cond = self._make_condition(self.visitFunctionCall(ctx.functionCall()), last=last)
                    self.emit_condition(cond)

                case _ if ctx.TRUE():
                    self._dbg("TRUE literal")
                    cond = self._make_condition("%{TRUE}", last=last)
                    self.emit_condition(cond)

                case _ if ctx.FALSE():
                    self._dbg("FALSE literal")
                    cond = self._make_condition("%{FALSE}", last=last)
                    self.emit_condition(cond)

                case _ if ctx.ident:
                    name = ctx.ident.text
                    symbol, default_expr = self._resolve_identifier_with_validation(name)

                    if default_expr:
                        cond_txt = f"{symbol} =\"\""
                        negate = not self._cond_state.not_
                    else:
                        cond_txt = symbol
                        negate = self._cond_state.not_

                    cond_txt = self._normalize_empty_string_condition(cond_txt, self._cond_state)
                    cond_txt = self._apply_with_modifiers(cond_txt, self._cond_state)

                    self._cond_state.not_ = False
                    self._dbg(f"{'implicit' if default_expr else 'explicit'} comparison: {cond_txt} negate={negate}")
                    cond = self._make_condition(cond_txt, last=last, negate=negate)
                    self.emit_condition(cond)
