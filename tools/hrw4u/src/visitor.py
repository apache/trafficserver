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
import re, sys
from dataclasses import dataclass

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.symbols import SymbolResolver, SymbolResolutionError
from hrw4u.errors import hrw4u_error
from hrw4u.states import CondState


@dataclass
class QueuedItem:
    text: str
    state: CondState
    indent: int


class HRW4UVisitor(hrw4uVisitor):
    INDENT_SPACES = 4
    _SUBSTITUTE_PATTERN = re.compile(
        r"""(?<!%)\{\s*(?P<func>[a-zA-Z_][a-zA-Z0-9_-]*)\s*\((?P<args>[^)]*)\)\s*\}
            |
            (?<!%)\{(?P<var>[^{}()]+)\}
        """,
        re.VERBOSE,
    )

    def __init__(self, filename: str = "<stdin>", debug: bool = False):
        self.filename = filename
        self.output: list[str] = []
        self.current_section: str | None = None

        self._cond_indent = 0
        self._stmt_indent = 0
        self._cond_state = CondState()
        self._queued: QueuedItem | None = None

        self._debug_enabled = debug
        self._debug_indent = 0

        self.symbol_resolver = SymbolResolver(False)  # ToDo: make this configurable

#
# Debugging
#

    def _debug(self, msg: str, levels: bool = False, out: bool = False):
        if self._debug_enabled:
            if levels:
                msg = f"</{msg}>" if out else f"<{msg}>"
            print("[debug] " + " " * (self._debug_indent * self.INDENT_SPACES) + msg, file=sys.stderr)

    def _debug_enter(self, msg: str):
        self._debug(msg, levels=True)
        self._debug_indent += 1

    def _debug_exit(self, msg: str | None = None):
        self._debug_indent = max(0, self._debug_indent - 1)
        self._debug(msg, levels=True, out=True)

    def _make_condition(self, cond_text: str, last: bool = False, negate: bool = False):
        self._debug(f"make_condition: {cond_text} last={last} negate={negate}")
        self._cond_state.not_ |= negate
        self._cond_state.last = last
        return f"cond {cond_text}"

#
# Helpers
#

    def _queue_condition(self, text: str):
        self._debug(f"queue cond: {text}  state={self._cond_state.to_list()}")
        self._queued = QueuedItem(text=text, state=self._cond_state.copy(), indent=self._cond_indent)
        self._cond_state.reset()

    def _flush_condition(self):
        if self._queued:
            mods = self._queued.state.to_list()
            self._debug(f"flush cond: {self._queued.text} state={mods} indent={self._queued.indent}")
            mod_str = f" [{','.join(mods)}]" if mods else ""
            self.output.append(" " * (self._queued.indent * self.INDENT_SPACES) + f"{self._queued.text}{mod_str}")
            self._queued = None

    def _parse_function_call(self, ctx):
        func = ctx.funcName.text
        args = [v.getText() for v in ctx.argumentList().value()] if ctx.argumentList() else []
        return func, args

    def _substitute_strings(self, s: str, ctx):
        inner = s[1:-1]
        for m in reversed(list(self._SUBSTITUTE_PATTERN.finditer(inner))):
            try:
                if m.group("func"):
                    func_name = m.group("func").strip()
                    arg_str = m.group("args").strip()
                    args = [arg.strip() for arg in arg_str.split(',')] if arg_str else []
                    replacement = self.symbol_resolver.resolve_function(func_name, args, strip_quotes=False)
                    self._debug(f"substitute: {{{func_name}({arg_str})}} -> {replacement}")
                elif m.group("var"):
                    var_name = m.group("var").strip()
                    replacement = self.symbol_resolver.resolve_condition(var_name, self.current_section)
                    self._debug(f"substitute: {{{var_name}}} -> {replacement}")
                else:
                    raise SymbolResolutionError(m.group(0), "Unrecognized substitution format")
            except Exception as e:
                raise hrw4u_error(self.filename, ctx, f"symbol error in {{}}: {e}")

            start, end = m.span()
            inner = inner[:start] + replacement + inner[end:]

        return f'"{inner}"'

#
# Visitors
#

    def visitProgram(self, ctx):
        self._debug_enter("visitProgram")
        try:
            for i, sec in enumerate(ctx.section()):
                start = len(self.output)
                self.visit(sec)
                if i < len(ctx.section()) - 1 and len(self.output) > start:
                    self.output.append("")
            self._flush_condition()
            return self.output
        finally:
            self._debug_exit("visitProgram")

    def visitSection(self, ctx):
        self._debug_enter("visitSection")
        if ctx.varSection():
            return self.visitVarSection(ctx.varSection())

        section = ctx.name.text
        self.current_section = section
        try:
            hook = self.symbol_resolver.map_hook(section)
            self._debug(f"`{section}' -> `{hook}'")
            emitted = False
            after_conditional = False

            for body in ctx.sectionBody():
                if body.conditional():
                    if emitted:
                        self._flush_condition()
                        self.output.append("")
                    self.emit_condition(f"cond %{{{hook}}} [AND]", final=True)
                    self.visit(body)
                    emitted = True
                    after_conditional = True

                elif body.statement():
                    if emitted and after_conditional:
                        self._flush_condition()
                        self.output.append("")
                        emitted = False
                    if not emitted:
                        self.emit_condition(f"cond %{{{hook}}} [AND]", final=True)
                        emitted = True
                    self._stmt_indent += 1
                    self.visit(body)
                    self._stmt_indent -= 1
                    after_conditional = False

        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)
        self._debug_exit("visitSection")

    def visitVarSection(self, ctx):
        if self.current_section is not None:
            raise hrw4u_error(self.filename, ctx, "Variable section must be first in a section")
        self._debug_enter("visitVarSection")
        self.visit(ctx.variables())
        self._debug_exit("visitVarSection")
        return

    def visitStatement(self, ctx):
        self._debug_enter("visitStatement")
        try:
            if ctx.BREAK():
                self._debug("BREAK")
                self.emit_statement("no-op [L]")
                return

            if ctx.functionCall():
                func, args = self._parse_function_call(ctx.functionCall())
                subst_args = [
                    self._substitute_strings(arg, ctx) if arg.startswith('"') and arg.endswith('"') else arg for arg in args
                ]
                symbol = self.symbol_resolver.resolve_statement_func(func, subst_args)
                self.emit_statement(symbol)
                return

            if ctx.EQUAL():
                lhs = ctx.lhs.text
                rhs = ctx.value().getText()
                if rhs.startswith('"') and rhs.endswith('"'):
                    rhs = self._substitute_strings(rhs, ctx)
                self._debug(f"assignment: {lhs} = {rhs}")
                lhs_obj = self.symbol_resolver.symbol_for(lhs)
                rhs_obj = self.symbol_resolver.symbol_for(rhs)
                if lhs_obj and rhs_obj:
                    if rhs_obj.var_type != lhs_obj.var_type:
                        raise SymbolResolutionError(rhs, f"Type mismatch: {lhs_obj.var_type} vs {rhs_obj.var_type}")
                    out = lhs_obj.as_operator(rhs_obj.as_cond())
                else:
                    out = self.symbol_resolver.resolve_assignment(lhs, rhs, self.current_section)
                self.emit_statement(out)
                return

            operator = ctx.op.text
            self._debug(f"standalone op: {operator}")
            cmd, validator = self.symbol_resolver.get_operator(operator)
            if validator:
                raise SymbolResolutionError(operator, "This operator requires an argument")
            self.emit_statement(cmd)
            return

        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)
        finally:
            self._debug_exit("visitStatement")

    def visitVariables(self, ctx):
        self._debug_enter("visitVariables")
        for decl in ctx.variableDecl():
            self.visit(decl)
        self._debug_exit("visitVariables")

    def visitVariableDecl(self, ctx):
        self._debug_enter("visitVariableDecl")
        try:
            name = ctx.name.text
            type = ctx.typeName.text
            symbol = self.symbol_resolver.declare_variable(name, type)
            self._debug(f"bind `{name}' to {symbol}")
        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)
        self._debug_exit("visitVariableDecl")

    def visitConditional(self, ctx):
        self._debug_enter("visitConditional")
        self.visit(ctx.ifStatement())
        for elif_ctx in ctx.elifClause():
            self.visit(elif_ctx)
        if ctx.elseClause():
            self.visit(ctx.elseClause())
        self._debug_exit("visitConditional")

    def visitIfStatement(self, ctx):
        self._debug_enter("visitIfStatement")
        self.visit(ctx.condition())
        self.visit(ctx.block())
        self._debug_exit("visitIfStatement")

    def visitElseClause(self, ctx):
        self._debug_enter("visitElseClause")
        self.emit_condition("else", final=True)
        self.visit(ctx.block())
        self._debug_exit("visitElseClause")

    def visitElifClause(self, ctx):
        self._debug_enter("visitElifClause")
        self.emit_condition("elif", final=True)
        self._stmt_indent += 1
        self._cond_indent += 1
        self.visit(ctx.condition())
        self.visit(ctx.block())
        self._stmt_indent -= 1
        self._cond_indent -= 1
        self._debug_exit("visitElifClause")

    def visitBlock(self, ctx):
        self._debug_enter("visitBlock")
        self._stmt_indent += 1
        for s in ctx.statement():
            self.visit(s)
        self._stmt_indent -= 1
        self._debug_exit("visitBlock")

    def visitCondition(self, ctx):
        self._debug_enter("visitCondition")
        self.emit_expression(ctx.expression(), last=True)
        self._flush_condition()
        self._debug_exit("visitCondition")

    def visitComparison(self, ctx, *, last: bool = False):
        self._debug_enter("visitComparison")
        comp = ctx.comparable()
        try:
            if comp.ident:
                lhs = self.symbol_resolver.resolve_condition(comp.ident.text, self.current_section)
            else:
                lhs = self.visitFunctionCall(comp.functionCall())
            operator = ctx.getChild(1)
            negate = operator.symbol.type in (hrw4uParser.NEQ, hrw4uParser.NOT_TILDE)

            if ctx.value():
                rhs = ctx.value().getText()
                if operator.symbol.type in (hrw4uParser.EQUALS, hrw4uParser.NEQ):
                    cond_txt = f"{lhs} ={rhs}"
                else:
                    cond_txt = f"{lhs} {operator.getText()}{rhs}"

            elif ctx.regex():
                cond_txt = f"{lhs} {ctx.regex().getText()}"

            # IP Ranges are a bit special, we keep the {} verbatim and no quotes allowed
            elif ctx.iprange():
                cond_txt = f"{lhs} {ctx.iprange().getText()}"

            elif ctx.set_():
                inner = ctx.set_().getText()[1:-1]
                # We no longer strip the quotes here for sets, fixed in #12256
                # parts = [s.strip().strip("'") for s in inner.split(",")]
                cond_txt = f"{lhs} ({inner})"

            else:
                raise hrw4u_error(self.filename, ctx, "Invalid comparison (should not happen)")

            if ctx.modifier():
                self.visit(ctx.modifier())
            self._cond_state.not_ = negate
            self._cond_state.last = last
            self._debug(f"comparison: {cond_txt}")
            self.emit_condition(f"cond {cond_txt}")
            self._debug_exit("visitComparison")

        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)

    def visitModifier(self, ctx):
        self.visit(ctx.modifierList())

    def visitModifierList(self, ctx):
        for tok in ctx.mods:
            try:
                mod = tok.text.upper()
                self._cond_state.add_modifier(mod)
            except Exception as e:
                raise hrw4u_error(self.filename, ctx, e)

    def visitFunctionCall(self, ctx):
        try:
            func, raw_args = self._parse_function_call(ctx)
            self._debug(f"function: {func}({', '.join(raw_args)})")
            return self.symbol_resolver.resolve_function(func, raw_args, strip_quotes=True)
        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)


#
# Emitters
#

    def emit_condition(self, text: str, *, final: bool = False):
        if final:
            self.output.append(" " * (self._cond_indent * self.INDENT_SPACES) + text)
        else:
            if self._queued:
                self._flush_condition()
            self._queue_condition(text)

    def emit_statement(self, line: str):
        self._flush_condition()
        self.output.append(" " * (self._stmt_indent * self.INDENT_SPACES) + line)

    def emit_expression(self, ctx, *, nested: bool = False, last: bool = False):
        self._debug_enter("emit_expression")
        if ctx.OR():
            self._debug("`OR' detected")
            if nested:
                self._debug("GROUP-START")
                self.emit_condition("cond %{GROUP}", final=True)
                self._cond_indent += 1

            self.emit_expression(ctx.expression(), nested=True, last=False)

            if self._queued:
                self._queued.indent = self._cond_indent
                self._queued.state.and_or = True
            self._flush_condition()

            self.emit_term(ctx.term(), last=last)

            if nested:
                self._flush_condition()
                self._cond_indent -= 1
                self.emit_condition("cond %{GROUP:END}")
        else:
            self.emit_term(ctx.term(), last=last)
        self._debug_exit("emit_expression")

    def emit_term(self, ctx, *, last: bool = False):
        self._debug_enter("emit_term")
        if ctx.AND():
            self._debug("`AND' detected")
            self.emit_term(ctx.term(), last=False)
            if self._queued:
                self._queued.indent = self._cond_indent
                self._queued.state.and_or = False
            self._flush_condition()
            self.emit_factor(ctx.factor(), last=last)
        else:
            self.emit_factor(ctx.factor(), last=last)
        self._debug_exit("emit_term")

    def emit_factor(self, ctx, *, last: bool = False):
        self._debug_enter("emit_factor")
        try:
            if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
                self._debug("`NOT' detected")
                self._cond_state.not_ = True
                self.emit_factor(ctx.getChild(1), last=last)

            elif ctx.LPAREN():
                self._debug("GROUP-START")
                self.emit_condition("cond %{GROUP}", final=True)
                self._cond_indent += 1
                self.emit_expression(ctx.expression(), nested=False, last=True)
                self._cond_indent -= 1
                self._cond_state.last = last
                self.emit_condition("cond %{GROUP:END}")

            elif ctx.comparison():
                self.visitComparison(ctx.comparison(), last=last)

            elif ctx.functionCall():
                cond = self._make_condition(self.visitFunctionCall(ctx.functionCall()), last=last)
                self.emit_condition(cond)

            elif ctx.TRUE():
                self._debug("TRUE literal")
                cond = self._make_condition("%{TRUE}", last=last)
                self.emit_condition(cond)

            elif ctx.FALSE():
                self._debug("FALSE literal")
                cond = self._make_condition("%{FALSE}", last=last)
                self.emit_condition(cond)

            elif ctx.ident:
                name = ctx.ident.text
                entry = self.symbol_resolver.symbol_for(name)
                if entry:
                    symbol = entry.as_cond()
                else:
                    symbol = self.symbol_resolver.resolve_condition(name, self.current_section)
                cond = self._make_condition(symbol, last=last)
                self.emit_condition(cond)

        except Exception as e:
            raise hrw4u_error(self.filename, ctx, e)
        self._debug_exit("emit_factor")
