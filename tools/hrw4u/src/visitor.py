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
from antlr4 import *
import re
import sys

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.symbols import SymbolResolver, SymbolResolutionError
from hrw4u.errors import wrap_error, Hrw4uSyntaxError
from hrw4u.states import CondState


class HRW4UVisitor(hrw4uVisitor):
    INDENT_SPACES = 4
    _SUBSTITUTE_PATTERN = re.compile(
        r"""(?<!%)\{
            \s*
            (?P<func>[a-zA-Z_][a-zA-Z0-9_-]*)
            \s*
            \((?P<args>[^)]*)\)
            \s*
            \}|
            (?<!%)\{(?P<var>[^{}()]+)\}
        """,
        re.VERBOSE,
    )

    def __init__(self, filename: str = "<input>", debug: bool = False):
        self.filename = filename
        self.output: list[str] = []
        self.current_section: str | None = None
        self.symbol_resolver = SymbolResolver()

        self._cond_indent = 0
        self._stmt_indent = 0
        self._cond_state = CondState()
        self._queued: tuple[str, CondState, int] | None = None

        self._debug_enabled = debug
        self._debug_indent = 0

    def _debug(self, msg: str | None = None):
        if self._debug_enabled and msg is not None:
            print(">>> " + "  " * self._debug_indent + msg, file=sys.stderr)

    def _debug_enter(self, msg: str):
        self._debug(msg)
        self._debug_indent += 1

    def _debug_exit(self, msg: str | None = None):
        self._debug_indent = max(0, self._debug_indent - 1)
        self._debug(msg)

    def _emit_indented(self, line: str, indent: int):
        self.output.append(" " * (indent * self.INDENT_SPACES) + line)

    def _queue_condition(self, text: str):
        self._debug(f"_queue_condition: {text}  state={self._cond_state.to_list()}")
        self._queued = (text, self._cond_state.copy(), self._cond_indent)
        self._cond_state.reset()

    def _flush_condition(self):
        if self._queued:
            text, cond_state, indent = self._queued
            mods = cond_state.to_list()
            self._debug(f"flush_condition: {text} mods={mods} indent={indent}")
            mod_str = f" [{','.join(mods)}]" if mods else ""
            self._emit_indented(f"{text}{mod_str}", indent)
            self._queued = None

    def _parse_function_call(self, ctx):
        func = ctx.funcName.text
        args = [v.getText() for v in ctx.argumentList().value()] if ctx.argumentList() else []
        return func, args

    def emit_condition(self, text: str, *, marker: bool = False):
        if marker:
            self._emit_indented(text, self._cond_indent)
        else:
            if self._queued:
                self._flush_condition()
            self._queue_condition(text)

    def emit_statement(self, line: str):
        self._flush_condition()
        self._emit_indented(line, self._stmt_indent)

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
        try:
            label = ctx.getChild(0).getText()

            if label == "VAR":
                self.current_section = "VAR"
                self._debug("VAR section")
                if ctx.variables():
                    self.visit(ctx.variables())
                self._debug_exit("visitSection")
                return

            self.current_section = label
            hook = self.symbol_resolver.map_hook(label)
            self._debug(f"{label} to {hook}")
            self.emit_condition(f"cond %{{{hook}}} [AND]", marker=True)

            if ctx.conditionalBlock():
                if ctx.conditionalBlock().block() and not ctx.conditionalBlock().ifStatement():
                    raise Hrw4uSyntaxError(self.filename, ctx.start.line, ctx.start.column, "Top-level block not allowed")
                self.visit(ctx.conditionalBlock())
            elif ctx.statementList():
                self._stmt_indent += 1
                for stmt in ctx.statementList().statement():
                    self.visit(stmt)
                self._stmt_indent -= 1
            else:
                raise Hrw4uSyntaxError(
                    self.filename, ctx.start.line, ctx.start.column, "Expected conditional block or statement list")
        except Exception as e:
            raise wrap_error(self.filename, ctx, e)
        finally:
            self._debug_exit("visitSection")

    def visitVariables(self, ctx):
        self._debug_enter("visitVariables")
        for d in ctx.variableDecl():
            self.visit(d)
        self._debug_exit("visitVariables")

    def visitVariableDecl(self, ctx):
        self._debug_enter("visitVariableDecl")
        try:
            name = ctx.name.text
            type = ctx.typeName.text
            symbol = self.symbol_resolver.declare_variable(name, type)
            self._debug(f"declare {name} to {symbol}")
        except Exception as e:
            raise wrap_error(self.filename, ctx, e)
        self._debug_exit("visitVariableDecl")

    def visitConditionalBlock(self, ctx):
        self._debug_enter("visitConditionalBlock")
        self.visit(ctx.ifStatement())
        if ctx.elseClause():
            self.visit(ctx.elseClause())
        self._debug_exit("visitConditionalBlock")

    def visitIfStatement(self, ctx):
        self._debug_enter("visitIfStatement")
        self.visit(ctx.condition())
        self.visit(ctx.block())
        self._debug_exit("visitIfStatement")

    def visitElseClause(self, ctx):
        self._debug_enter("visitElseClause")
        self.emit_condition("else", marker=True)
        self.visit(ctx.block())
        self._debug_exit("visitElseClause")

    def visitBlock(self, ctx):
        self._debug_enter("visitBlock")
        self._stmt_indent += 1
        for s in ctx.statement():
            self.visit(s)
        self._stmt_indent -= 1
        self._debug_exit("visitBlock")

    def visitStatement(self, ctx):
        self._debug_enter("visitStatement")
        try:
            if not ctx.SEMICOLON():
                raise Hrw4uSyntaxError(self.filename, ctx.start.line, ctx.start.column, "Malformed statement")

            if ctx.BREAK():
                self._debug("break statement")
                self.emit_statement("no-op [L]")
                return

            if ctx.functionCall():
                func, args = self._parse_function_call(ctx.functionCall())
                symbol = self.symbol_resolver.resolve_statement_func(func, args)
                self.emit_statement(symbol)
                return

            name = ctx.lhs.text if ctx.lhs else None
            if not name:
                raise Hrw4uSyntaxError(self.filename, ctx.start.line, ctx.start.column, "Missing identifier")

            if ctx.EQUAL():
                raw = ctx.value().getText()
                self._debug(f"assignment {name} = {raw}")
                if raw.startswith('"') and raw.endswith('"'):
                    raw = self.substitute_string(raw, ctx)
                sym_obj = self.symbol_resolver.symbol_for(name)
                if sym_obj:
                    out = sym_obj.as_operator(raw)
                else:
                    out = self.symbol_resolver.resolve_assignment(name, raw, self.current_section)
                self.emit_statement(out)
            else:
                self._debug(f"standalone operator {name}")
                cmd, validator = self.symbol_resolver.get_operator(name)
                if validator:
                    raise SymbolResolutionError(name, "This operator requires an argument")
                self.emit_statement(cmd)

        except Exception as e:
            raise wrap_error(self.filename, ctx, e)
        self._debug_exit("visitStatement")

    def visitCondition(self, ctx):
        self._debug_enter("visitCondition")
        self._cond_indent = 0
        self.emit_expression(ctx.logicalExpression(), last=True)
        self._flush_condition()
        self._debug_exit("visitCondition")

    def emit_expression(self, ctx, *, nested: bool = False, last: bool = False):
        self._debug_enter("emit_expression")
        if ctx.OR():
            self._debug("OR detected")
            if nested:
                self._debug("GROUP START")
                self.emit_condition("cond %{GROUP}", marker=True)
                self._cond_indent += 1

            self.emit_expression(ctx.logicalExpression(), nested=True, last=False)

            if self._queued:
                self._queued = (self._queued[0], self._queued[1].copy(), self._cond_indent)
                self._queued[1].and_or = True
            self._flush_condition()

            self.emit_term(ctx.logicalTerm(), last=last)

            if nested:
                self._flush_condition()
                self._cond_indent -= 1
                self.emit_condition("cond %{GROUP:END}")
        else:
            self.emit_term(ctx.logicalTerm(), last=last)
        self._debug_exit("emit_expression")

    def emit_term(self, ctx, *, nested: bool = False, last: bool = False):
        self._debug_enter("emit_term")
        if ctx.AND():
            self._debug("AND detected")
            self.emit_term(ctx.logicalTerm(), nested=True, last=False)
            if self._queued:
                self._queued = (self._queued[0], self._queued[1].copy(), self._cond_indent)
                self._queued[1].and_or = False
            self._flush_condition()
            self.emit_factor(ctx.logicalFactor(), last=last)
        else:
            self.emit_factor(ctx.logicalFactor(), last=last)
        self._debug_exit("emit_term")

    def emit_factor(self, ctx, *, last: bool = False):
        self._debug_enter("emit_factor")
        try:
            if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
                self._debug("NOT")
                self._cond_state.not_ = True
                self.emit_factor(ctx.getChild(1), last=last)
            elif ctx.LPAREN():
                self._debug("GROUP START")
                self.emit_condition("cond %{GROUP}", marker=True)
                self._cond_indent += 1
                self.emit_expression(ctx.logicalExpression(), nested=False, last=True)
                self._cond_indent -= 1
                self._cond_state.last = last
                self.emit_condition("cond %{GROUP:END}")
            elif ctx.comparison():
                self.visitComparison(ctx.comparison(), last=last)
            elif ctx.functionCall():
                self.add_cond(self.visitFunctionCall(ctx.functionCall()), last=last)
            elif ctx.TRUE():
                self._debug("TRUE literal")
                self.add_cond("%{TRUE}", last=last)
            elif ctx.FALSE():
                self._debug("FALSE literal")
                self.add_cond("%{FALSE}", last=last)
            elif ctx.ident:
                name = ctx.ident.text
                entry = self.symbol_resolver.symbol_for(name)
                symbol = entry.as_cond() if entry else self.symbol_resolver.resolve_condition(name, self.current_section)
                self.add_cond(symbol, last=last)
        except Exception as e:
            raise wrap_error(self.filename, ctx, e)
        self._debug_exit("emit_factor")

    def visitFunctionCall(self, ctx):
        try:
            func, raw_args = self._parse_function_call(ctx)
            self._debug(f"visitFunctionCall: {func}({', '.join(raw_args)})")
            return self.symbol_resolver.resolve_function(func, raw_args, strip_quotes=True)
        except Exception as e:
            raise wrap_error(self.filename, ctx, e)

    def visitModifier(self, ctx):
        self.visit(ctx.modifierList())

    def visitModifierList(self, ctx):
        for tok in ctx.mods:
            try:
                mod = tok.text.upper()
                self._cond_state.add_modifier(mod)
            except Exception as e:
                raise wrap_error(self.filename, ctx, e)

    def visitComparison(self, ctx, *, last: bool = False):
        try:
            comp = ctx.comparable()
            lhs = (
                self.symbol_resolver.resolve_condition(comp.ident.text, self.current_section)
                if comp.ident else self.visitFunctionCall(comp.functionCall()))
            operator = ctx.getChild(1)
            token_type = operator.symbol.type
            operator_text = operator.getText()
            self._debug(f"visitComparison: {lhs} {operator_text} …")
            negate = token_type in (hrw4uParser.NEQ, hrw4uParser.NOT_TILDE)
            if ctx.value():
                rhs = ctx.value().getText()
                cond_txt = f"{lhs} ={rhs}" if token_type in (hrw4uParser.EQUALS, hrw4uParser.NEQ) else f"{lhs} {operator_text}{rhs}"
            elif ctx.regex():
                cond_txt = f"{lhs} {ctx.regex().getText()}"
            else:
                raw = ctx.getChild(2).getText()
                if raw.startswith("[") and raw.endswith("]"):
                    inner = raw[1:-1]
                    parts = [s.strip().strip('"').strip("'") for s in inner.split(",")]
                    cond_txt = f"{lhs} ({','.join(parts)})"
                else:
                    cond_txt = f"{lhs} {raw}"
            if ctx.modifier():
                self.visit(ctx.modifier())
            self._cond_state.not_ = negate
            self._cond_state.last = last
            self.emit_condition(f"cond {cond_txt}")

        except Exception as e:
            raise wrap_error(self.filename, ctx, e)

    def add_cond(self, cond_text: str, last: bool = False, negate: bool = False):
        self._debug(f"add_cond: {cond_text} last={last} negate={negate}")
        self._cond_state.not_ |= negate
        self._cond_state.last = last
        if not cond_text.startswith("cond "):
            cond_text = f"cond {cond_text}"
        self.emit_condition(cond_text)

    def substitute_string(self, s: str, ctx):
        self._debug(f"substitute_string: {s}")
        inner = s[1:-1]

        for m in reversed(list(self._SUBSTITUTE_PATTERN.finditer(inner))):
            try:
                if m.group("func"):
                    func_name = m.group("func").strip()
                    arg_str = m.group("args").strip()
                    args = [arg.strip() for arg in arg_str.split(',')] if arg_str else []
                    replacement = self.symbol_resolver.resolve_function(func_name, args, strip_quotes=False)
                    self._debug(f"substitute: {{{func_name}({arg_str})}} to {replacement}")
                elif m.group("var"):
                    var_name = m.group("var").strip()
                    replacement = self.symbol_resolver.resolve_condition(var_name, self.current_section)
                    self._debug(f"substitute: {{{var_name}}} to {replacement}")
                else:
                    raise SymbolResolutionError(m.group(0), "Unrecognized substitution format")
            except Exception as e:
                raise wrap_error(self.filename, ctx, f"symbol error in {{}}: {e}")

            start, end = m.span()
            inner = inner[:start] + replacement + inner[end:]

        return f'"{inner}"'
