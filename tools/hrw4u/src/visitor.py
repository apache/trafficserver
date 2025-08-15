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

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.symbols import SymbolResolver, SymbolResolutionError
from hrw4u.errors import hrw4u_error
from hrw4u.states import CondState, SectionType
from hrw4u.debugging import Dbg
from hrw4u.common import VisitorMixin, RegexPatterns, SystemDefaults


@dataclass(slots=True)
class QueuedItem:
    text: str
    state: CondState
    indent: int


class HRW4UVisitor(hrw4uVisitor, VisitorMixin):
    _SUBSTITUTE_PATTERN = RegexPatterns.SUBSTITUTE_PATTERN

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None) -> None:
        self.filename = filename
        self.output: list[str] = []
        self.current_section: SectionType | None = None
        self.error_collector = error_collector

        self._cond_indent = 0
        self._stmt_indent = 0
        self._cond_state = CondState()
        self._queued: QueuedItem | None = None

        self._dbg = Dbg(debug)

        self.symbol_resolver = SymbolResolver(SystemDefaults.DEFAULT_CONFIGURABLE)  # TODO: make this configurable

    def _make_condition(self, cond_text: str, last: bool = False, negate: bool = False) -> str:
        self._dbg(f"make_condition: {cond_text} last={last} negate={negate}")
        self._cond_state.not_ |= negate
        self._cond_state.last = last
        return f"cond {cond_text}"

#
# Helpers
#

    def _queue_condition(self, text: str) -> None:
        self._dbg(f"queue cond: {text}  state={self._cond_state.to_list()}")
        self._queued = QueuedItem(text=text, state=self._cond_state.copy(), indent=self._cond_indent)
        self._cond_state.reset()

    def _flush_condition(self) -> None:
        if self._queued:
            mods = self._queued.state.to_list()
            self._dbg(f"flush cond: {self._queued.text} state={mods} indent={self._queued.indent}")
            mod_str = f" [{','.join(mods)}]" if mods else ""
            self.output.append(self.format_with_indent(f"{self._queued.text}{mod_str}", self._queued.indent))
            self._queued = None

    def _parse_function_call(self, ctx) -> tuple[str, list[str]]:
        func = ctx.funcName.text
        args = [v.getText() for v in ctx.argumentList().value()] if ctx.argumentList() else []
        return func, args

    def _substitute_strings(self, s: str, ctx) -> str:
        inner = s[1:-1]

        def repl(m: re.Match) -> str:
            try:
                if m.group("func"):
                    func_name = m.group("func").strip()
                    arg_str = m.group("args").strip()
                    args = [arg.strip() for arg in arg_str.split(',')] if arg_str else []
                    replacement = self.symbol_resolver.resolve_function(func_name, args, strip_quotes=False)
                    self._dbg(f"substitute: {{{func_name}({arg_str})}} -> {replacement}")
                    return replacement
                if m.group("var"):
                    var_name = m.group("var").strip()
                    replacement, _ = self.symbol_resolver.resolve_condition(var_name, self.current_section)
                    self._dbg(f"substitute: {{{var_name}}} -> {replacement}")
                    return replacement
                raise SymbolResolutionError(m.group(0), "Unrecognized substitution format")
            except Exception as e:
                error = hrw4u_error(self.filename, ctx, f"symbol error in {{}}: {e}")
                if self.error_collector:
                    self.error_collector.add_error(error)
                    return f"{{ERROR: {e}}}"  # Return placeholder to continue processing
                else:
                    raise error

        return f'"{self._SUBSTITUTE_PATTERN.sub(repl, inner)}"'

#
# Visitors
#

    def visitProgram(self, ctx) -> list[str]:
        self._dbg.enter("visitProgram")
        try:
            sections = ctx.section()
            section_count = len(sections)
            for idx, section in enumerate(sections):
                start_length = len(self.output)
                self.visit(section)
                if idx < section_count - 1 and len(self.output) > start_length:
                    self.output.append("")
            self._flush_condition()
            return self.output
        finally:
            self._dbg.exit("visitProgram")

    def visitSection(self, ctx) -> None:
        self._dbg.enter("visitSection")
        if ctx.varSection():
            return self.visitVarSection(ctx.varSection())

        section_name = ctx.name.text
        try:
            self.current_section = SectionType(section_name)
        except ValueError:
            error = hrw4u_error(self.filename, ctx, f"Invalid section name: '{section_name}'")
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Skip processing this section but continue
            else:
                raise error
        try:
            hook = self.symbol_resolver.map_hook(section_name)
            self._dbg(f"`{section_name}' -> `{hook}'")
            in_statement_block = False
            for idx, body in enumerate(ctx.sectionBody()):
                is_conditional = body.conditional() is not None
                if is_conditional or not in_statement_block:
                    if idx > 0:  # Add a newline if this is not the first element.
                        self._flush_condition()
                        self.output.append("")
                    self.emit_condition(f"cond %{{{hook}}} [AND]", final=True)

                if is_conditional:
                    self.visit(body)
                    in_statement_block = False
                else:
                    in_statement_block = True
                    self._stmt_indent += 1
                    self.visit(body)
                    self._stmt_indent -= 1

        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Continue processing other sections
            else:
                raise error
        self._dbg.exit("visitSection")

    def visitVarSection(self, ctx) -> None:
        if self.current_section is not None:
            error = hrw4u_error(self.filename, ctx, "Variable section must be first in a section")
            if self.error_collector:
                self.error_collector.add_error(error)
                return
            else:
                raise error
        self._dbg.enter("visitVarSection")
        self.visit(ctx.variables())
        self._dbg.exit("visitVarSection")

    def visitStatement(self, ctx) -> None:
        self._dbg.enter("visitStatement")
        try:
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
                    symbol = self.symbol_resolver.resolve_statement_func(func, subst_args)
                    self.emit_statement(symbol)
                    return

                case _ if ctx.EQUAL():
                    lhs = ctx.lhs.text
                    rhs = ctx.value().getText()
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    self._dbg(f"assignment: {lhs} = {rhs}")
                    out = self.symbol_resolver.resolve_assignment(lhs, rhs, self.current_section)
                    self.emit_statement(out)
                    return

                case _:
                    operator = ctx.op.text
                    self._dbg(f"standalone op: {operator}")
                    cmd, validator = self.symbol_resolver.get_statement_spec(operator)
                    if validator:
                        raise SymbolResolutionError(operator, "This operator requires an argument")
                    self.emit_statement(cmd)
                    return

        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Continue processing other statements
            else:
                raise error
        finally:
            self._dbg.exit("visitStatement")

    def visitVariables(self, ctx) -> None:
        self._dbg.enter("visitVariables")
        for decl in ctx.variableDecl():
            self.visit(decl)
        self._dbg.exit("visitVariables")

    def visitVariableDecl(self, ctx) -> None:
        self._dbg.enter("visitVariableDecl")
        try:
            name = ctx.name.text
            type = ctx.typeName.text
            symbol = self.symbol_resolver.declare_variable(name, type)
            self._dbg(f"bind `{name}' to {symbol}")
        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Continue processing other variables
            else:
                raise error
        self._dbg.exit("visitVariableDecl")

    def visitConditional(self, ctx) -> None:
        self._dbg.enter("visitConditional")
        self.visit(ctx.ifStatement())
        for elif_ctx in ctx.elifClause():
            self.visit(elif_ctx)
        if ctx.elseClause():
            self.visit(ctx.elseClause())
        self._dbg.exit("visitConditional")

    def visitIfStatement(self, ctx) -> None:
        self._dbg.enter("visitIfStatement")
        self.visit(ctx.condition())
        self.visit(ctx.block())
        self._dbg.exit("visitIfStatement")

    def visitElseClause(self, ctx) -> None:
        self._dbg.enter("visitElseClause")
        self.emit_condition("else", final=True)
        self.visit(ctx.block())
        self._dbg.exit("visitElseClause")

    def visitElifClause(self, ctx) -> None:
        self._dbg.enter("visitElifClause")
        self.emit_condition("elif", final=True)
        self._stmt_indent += 1
        self._cond_indent += 1
        self.visit(ctx.condition())
        self.visit(ctx.block())
        self._stmt_indent -= 1
        self._cond_indent -= 1
        self._dbg.exit("visitElifClause")

    def visitBlock(self, ctx) -> None:
        self._dbg.enter("visitBlock")
        self._stmt_indent += 1
        for s in ctx.statement():
            self.visit(s)
        self._stmt_indent -= 1
        self._dbg.exit("visitBlock")

    def visitCondition(self, ctx) -> None:
        self._dbg.enter("visitCondition")
        self.emit_expression(ctx.expression(), last=True)
        self._flush_condition()
        self._dbg.exit("visitCondition")

    def visitComparison(self, ctx, *, last: bool = False) -> None:
        self._dbg.enter("visitComparison")
        comp = ctx.comparable()
        try:
            if comp.ident:
                lhs, _ = self.symbol_resolver.resolve_condition(comp.ident.text, self.current_section)
            else:
                lhs = self.visitFunctionCall(comp.functionCall())
            operator = ctx.getChild(1)
            negate = operator.symbol.type in (hrw4uParser.NEQ, hrw4uParser.NOT_TILDE)

            match ctx:
                case _ if ctx.value():
                    rhs = ctx.value().getText()
                    match operator.symbol.type:
                        case hrw4uParser.EQUALS | hrw4uParser.NEQ:
                            cond_txt = f"{lhs} ={rhs}"
                        case _:
                            cond_txt = f"{lhs} {operator.getText()}{rhs}"

                case _ if ctx.regex():
                    cond_txt = f"{lhs} {ctx.regex().getText()}"

                # IP Ranges are a bit special, we keep the {} verbatim and no quotes allowed
                case _ if ctx.iprange():
                    cond_txt = f"{lhs} {ctx.iprange().getText()}"

                case _ if ctx.set_():
                    inner = ctx.set_().getText()[1:-1]
                    # We no longer strip the quotes here for sets, fixed in #12256
                    # parts = [s.strip().strip("'") for s in inner.split(",")]
                    cond_txt = f"{lhs} ({inner})"

                case _:
                    raise hrw4u_error(self.filename, ctx, "Invalid comparison (should not happen)")

            if ctx.modifier():
                self.visit(ctx.modifier())
            self._cond_state.not_ = negate
            self._cond_state.last = last
            self._dbg(f"comparison: {cond_txt}")
            self.emit_condition(f"cond {cond_txt}")
            self._dbg.exit("visitComparison")

        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Continue processing other comparisons
            else:
                raise error

    def visitModifier(self, ctx) -> None:
        self.visit(ctx.modifierList())

    def visitModifierList(self, ctx) -> None:
        for token in ctx.mods:
            try:
                mod = token.text.upper()
                self._cond_state.add_modifier(mod)
            except Exception as exc:
                error = hrw4u_error(self.filename, ctx, exc)
                if self.error_collector:
                    self.error_collector.add_error(error)
                    return  # Continue processing other modifiers
                else:
                    raise error

    def visitFunctionCall(self, ctx) -> str:
        try:
            func, raw_args = self._parse_function_call(ctx)
            self._dbg(f"function: {func}({', '.join(raw_args)})")
            return self.symbol_resolver.resolve_function(func, raw_args, strip_quotes=True)
        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return "ERROR"  # Return placeholder to continue processing
            else:
                raise error


#
# Emitters
#

    def emit_condition(self, text: str, *, final: bool = False) -> None:
        if final:
            self.output.append(self.format_with_indent(text, self._cond_indent))
        else:
            if self._queued:
                self._flush_condition()
            self._queue_condition(text)

    def emit_statement(self, line: str) -> None:
        self._flush_condition()
        self.output.append(self.format_with_indent(line, self._stmt_indent))

    def emit_expression(self, ctx, *, nested: bool = False, last: bool = False, grouped: bool = False) -> None:
        self._dbg.enter("emit_expression")
        if ctx.OR():
            self._dbg("`OR' detected")
            if grouped:
                self._dbg("GROUP-START")
                self.emit_condition("cond %{GROUP}", final=True)
                self._cond_indent += 1

            self.emit_expression(ctx.expression(), nested=False, last=False)
            if self._queued:
                self._queued.state.and_or = True
            self._flush_condition()
            self.emit_term(ctx.term(), last=last)

            if grouped:
                self._flush_condition()
                self._cond_indent -= 1
                self.emit_condition("cond %{GROUP:END}")
        else:
            self.emit_term(ctx.term(), last=last)
        self._dbg.exit("emit_expression")

    def emit_term(self, ctx, *, last: bool = False) -> None:
        self._dbg.enter("emit_term")
        if ctx.AND():
            self._dbg("`AND' detected")
            self.emit_term(ctx.term(), last=False)
            if self._queued:
                self._queued.indent = self._cond_indent
                self._queued.state.and_or = False
            self._flush_condition()
            self.emit_factor(ctx.factor(), last=last)
        else:
            self.emit_factor(ctx.factor(), last=last)
        self._dbg.exit("emit_term")

    def emit_factor(self, ctx, *, last: bool = False) -> None:
        self._dbg.enter("emit_factor")
        try:
            match ctx:
                case _ if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
                    self._dbg("`NOT' detected")
                    self._cond_state.not_ = True
                    self.emit_factor(ctx.getChild(1), last=last)

                case _ if ctx.LPAREN():
                    self._dbg("GROUP-START")
                    self.emit_condition("cond %{GROUP}", final=True)
                    self._cond_indent += 1
                    self.emit_expression(ctx.expression(), nested=False, last=True, grouped=True)
                    self._cond_indent -= 1
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
                    if entry := self.symbol_resolver.symbol_for(name):
                        symbol = entry.as_cond()
                        default_expr = False
                    else:
                        symbol, default_expr = self.symbol_resolver.resolve_condition(name, self.current_section)

                    if default_expr:
                        cond_txt = f"{symbol} =\"\""
                        negate = not self._cond_state.not_
                    else:
                        cond_txt = symbol
                        negate = self._cond_state.not_

                    self._cond_state.not_ = False
                    self._dbg(f"{'implicit' if default_expr else 'explicit'} comparison: {cond_txt} negate={negate}")
                    cond = self._make_condition(cond_txt, last=last, negate=negate)
                    self.emit_condition(cond)

        except Exception as e:
            error = hrw4u_error(self.filename, ctx, e)
            if self.error_collector:
                self.error_collector.add_error(error)
                return  # Continue processing other factors
            else:
                raise error
        self._dbg.exit("emit_factor")
