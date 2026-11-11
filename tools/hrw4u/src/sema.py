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
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
from typing import Callable

import hrw4u.ast_nodes as ast
import hrw4u.sema_nodes as ir
import hrw4u.types as types
from hrw4u.common import RegexPatterns, SystemDefaults, parse_mods, split_interpolation_mods
from hrw4u.debugging import Dbg
from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError, SourceMap, SymbolResolutionError, Warning, hrw4u_error_at
from hrw4u.procedures import ProcEntry, ProcRegistry
from hrw4u.sandbox import SandboxConfig, SandboxDenialError
from hrw4u.states import CondState, SectionType, VALUE_MODIFIERS
from hrw4u.symbols import SymbolResolver
from hrw4u.validation import Validator

_VAR_SECTIONS = {
    "txn": ("Variable", "VARS", types.VarScope.TXN),
    "session": ("Session variable", "SESSION_VARS", types.VarScope.SESSION),
}

_PARAM_REF = re.compile(r'\$([a-zA-Z_][a-zA-Z0-9_-]*)')


def _strip_quotes(text: str) -> str:
    if text.startswith('"') and text.endswith('"'):
        return text[1:-1]
    return text


# _split_args and _unescape_double_braces are copied from HRW4UVisitor rather than shared: its copies go with the CST emitter.
def _split_args(arg_str: str) -> list[str]:
    if not arg_str.strip():
        return []

    args = []
    current_arg = []
    paren_depth = 0
    in_quotes = False
    quote_char = None

    for i, char in enumerate(arg_str):
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
            if char == quote_char and (i == 0 or arg_str[i - 1] != '\\'):
                in_quotes = False
                quote_char = None

    if current_arg:
        args.append(''.join(current_arg).strip())

    return args


def _unescape_double_braces(escaped: str) -> str:
    """`{{...}}` in a string literal: the payload as literal `{...}`, unescaped quotes escaped."""
    out: list[str] = []
    backslashes = 0
    for char in escaped[2:-2]:
        if char == '"' and backslashes % 2 == 0:
            out.append('\\')
        out.append(char)
        if char == '\\':
            backslashes += 1
        else:
            backslashes = 0
    return "{" + "".join(out) + "}"


class Sema:
    """Resolves an HRW4U AST into a backend-neutral tree.

    Sandbox checks run during resolution, here and in SymbolResolver.lookup_*, rather than as one
    pass over the finished tree: the CST emitter checks as it goes and drops what it denies, and
    parity holds sema to the same diagnostics in the same order. Once the CLI routes through sema
    and the CST emitter is gone, they can move to a single pass after resolution.
    """

    def __init__(
        self,
        *,
        debug: bool = SystemDefaults.DEFAULT_DEBUG,
        error_collector: ErrorCollector | None = None,
        preserve_comments: bool = True,
        proc_search_paths: list[Path] | None = None,
        sandbox: SandboxConfig | None = None,
    ) -> None:
        self.error_collector = error_collector
        self.preserve_comments = preserve_comments
        self.source_map = SourceMap()
        self._sandbox = sandbox or SandboxConfig.empty()
        self._dbg = Dbg(debug)
        self.symbol_resolver = SymbolResolver(debug, sandbox=self._sandbox, dbg=self._dbg)
        self._proc_search_paths: list[Path] = list(proc_search_paths or [])
        self._section: SectionType | None = None
        self.procs = ProcRegistry(source_map=self.source_map)
        self._proc_stack: list[str] = []

    def _report(self, span: ast.Span, exc: Exception, *, note: str | None = None) -> None:
        error = hrw4u_error_at(span, exc, self.source_map.line_at(span))
        if note:
            error.add_note(note)
        if self.error_collector is None:
            raise error
        self.error_collector.add_error(error)
        if isinstance(exc, SandboxDenialError) and exc.sandbox_message:
            self.error_collector.set_sandbox_message(exc.sandbox_message)

    @contextmanager
    def trap(self, span: ast.Span, *, note: str | None = None):
        try:
            yield
        except Exception as exc:
            self._report(span, exc, note=note)

    def _warn(self, span: ast.Span, message: str) -> None:
        if self.error_collector is not None:
            self.error_collector.add_warning(Warning.from_span(span, message, self.source_map.line_at(span)))
            if self._sandbox.message:
                self.error_collector.set_sandbox_message(self._sandbox.message)

    def _drain_resolver_warnings(self, span: ast.Span) -> None:
        for warning in self.symbol_resolver.drain_warnings():
            self._warn(span, warning)

    def _unbound_param(self, name: str, span: ast.Span) -> Hrw4uSyntaxError:
        return self.source_map.error_at(span, f"'${name}' used outside procedure context")

    def _sandbox_check(self, span: ast.Span, check_fn: Callable[[], str | None]) -> bool:
        try:
            if warning := check_fn():
                self._warn(span, warning)
            return True
        except SandboxDenialError as exc:
            self._report(span, exc)
            return False

    def analyze(self, tree: ast.HRW4UAST) -> ir.Program:
        items: list[ir.ResolvedSection | ir.VarsBlock | ir.CommentStmt] = []
        seen_sections = False

        for node in tree.body:
            match node:
                case ast.Comment():
                    if self.preserve_comments:
                        items.append(ir.CommentStmt(text=node.text, span=node.span))
                case ast.VarSection():
                    seen_sections = True
                    items.append(self._declare_vars(node))
                case ast.UseDirective():
                    if seen_sections:
                        self._report(node.span, ValueError("'use' directives must appear before any section blocks"))
                        continue
                    with self.trap(node.span):
                        self.procs.load(node.spec, node.span, self._proc_search_paths)
                case ast.ProcedureDecl():
                    if seen_sections:
                        self._report(node.span, ValueError("'procedure' declarations must appear before any section blocks"))
                        continue
                    with self.trap(node.span):
                        self.procs.register(node)
                case ast.Section():
                    seen_sections = True
                    if section := self._section_of(node):
                        items.append(section)

        return ir.Program(items=tuple(items))

    def _section_of(self, node: ast.Section) -> ir.ResolvedSection | None:
        with self.trap(node.span):
            if warning := self._sandbox.check_section(node.type):
                self._warn(node.span, warning)
            try:
                self._section = SectionType(node.type)
            except ValueError:
                valid = ', '.join(s.value for s in SectionType)
                raise ValueError(f"Invalid section name: '{node.type}'. Valid sections: {valid}") from None

            hook = self._section.hook_name
            self._dbg(f"`{node.type}' -> `{hook}'")
            return ir.ResolvedSection(section=self._section, body=self._body(node.body), span=node.span)
        return None

    def _body(self, nodes: tuple[ast.BodyNode, ...]) -> tuple[ir.Stmt, ...]:
        out: list[ir.Stmt] = []
        for node in nodes:
            # Per statement, not per section: a denial or a resolution failure costs that
            # statement and not everything around it.
            with self.trap(node.span):
                self._body_item(node, out)
        return tuple(out)

    def _body_item(self, node: ast.BodyNode, out: list[ir.Stmt]) -> None:
        match node:
            case ast.Comment():
                if self.preserve_comments:
                    out.append(ir.CommentStmt(text=node.text, span=node.span))
            case ast.Break():
                if warning := self._sandbox.check_language("break"):
                    self._warn(node.span, warning)
                out.append(ir.BreakStmt(span=node.span))
            case ast.Assignment():
                if stmt := self._assignment(node):
                    out.append(stmt)
            case ast.FunctionCall() if self.procs.get(node.name) is not None:
                with self._inlining(node) as inlined:
                    if inlined is None:
                        return
                    # The first source item is resolved alone so that Inlined can say whether body[0] is it.
                    head, rest = self._body(inlined[:1]), self._body(inlined[1:])
                    opens_with_statement = bool(head) and not isinstance(head[0], (ir.CommentStmt, ir.Inlined))
                out.append(ir.Inlined(body=head + rest, opens_with_statement=opens_with_statement, span=node.span))
            case ast.FunctionCall():
                if stmt := self._statement_call(node):
                    out.append(stmt)
            case ast.IfBlock():
                out.append(self._if_block(node))

    def _if_block(self, node: ast.IfBlock) -> ir.If:
        # Source order, so that without a collector the error raised is the first one in the file.
        cond = self._condition(node.condition)
        body = self._body(node.body)

        elifs = []
        for arm in node.elif_branches:
            if self._sandbox_check(arm.span, lambda: self._sandbox.check_language("elif")):
                elifs.append(ir.ElifArm(cond=self._condition(arm.condition), body=self._body(arm.body), span=arm.span))

        else_body: tuple[ir.Stmt, ...] = ()
        else_span = node.else_span or node.span
        has_else = node.has_else and self._sandbox_check(else_span, lambda: self._sandbox.check_language("else"))
        if has_else:
            else_body = self._body(node.else_body)

        return ir.If(cond=cond, body=body, elifs=tuple(elifs), else_body=else_body, span=node.span, has_else=has_else)

    def _condition(self, node: ast.ConditionExpr) -> ir.Cond:
        match node:
            case ast.LogicalOp():
                if node.operator == "||":
                    modifier, combine = "OR", ir.Or
                else:
                    modifier, combine = "AND", ir.And
                if not self._sandbox_check(node.span, lambda: self._sandbox.check_modifier(modifier)):
                    return ir.Unresolved(span=node.span)
                left, right = self._condition(node.left), self._condition(node.right)
                return combine(left=left, right=right, span=node.span)
            case ast.NotOp():
                if not self._sandbox_check(node.span, lambda: self._sandbox.check_modifier("NOT")):
                    return ir.Unresolved(span=node.span)
                return ir.Not(inner=self._condition(node.operand), span=node.span)
            case ast.Group():
                return ir.Group(inner=self._condition(node.inner), span=node.span)
            case ast.BoolLiteral():
                return ir.BoolCond(value=node.value, span=node.span)
            case ast.FunctionCall():
                return self._func_cond(node)
            case ast.IdentCondition():
                return self._truth(node)
            case ast.Comparison():
                return self._test(node)
        raise SymbolResolutionError("condition", f"Unsupported condition {node!r}")

    def _func_cond(self, node: ast.FunctionCall) -> ir.Cond:
        with self.trap(node.span):
            # Source text, not resolved values: a `{...}` in a condition function's string argument
            # is not interpolated.
            args = [self._source_text(a, node.span) for a in node.args]
            # Validate before stripping quotes: quoted_or_simple() needs to see the quotes on values
            # such as file paths.
            key, entry = self.symbol_resolver.lookup_function(node.name, args)
            self._drain_resolver_warnings(node.span)
            return ir.FuncCond(key=key, entry=entry, args=tuple(ir.RawArg(text=_strip_quotes(a)) for a in args), span=node.span)
        return ir.Unresolved(span=node.span)

    def _truth(self, node: ast.IdentCondition) -> ir.Cond:
        with self.trap(node.span):
            ref, existence = self._reference(node.name, node.span)
            return ir.Truth(ref=ref, existence=existence, span=node.span)
        return ir.Unresolved(span=node.span)

    def _test(self, node: ast.Comparison) -> ir.Cond:
        with self.trap(node.span):
            if isinstance(node.left, ast.FunctionCall):
                func = self._func_cond(node.left)
                if not isinstance(func, ir.FuncCond):
                    return ir.Unresolved(span=node.span)  # _func_cond already reported
                ref = ir.BuiltinRef.from_function(func.key, func.entry, [a.text for a in func.args])
            else:
                ref, _ = self._reference(node.left.raw, node.span)

            op = ir.CmpOp(node.operator)

            if op in ir.NEGATED_OPS and not self._sandbox_check(node.span, lambda: self._sandbox.check_modifier("NOT")):
                return ir.Unresolved(span=node.span)

            if op in (ir.CmpOp.IN, ir.CmpOp.NOT_IN):
                rhs = self._operand(node.right, node.span)
                if not self._sandbox_check(node.span, lambda: self._sandbox.check_language("in")):
                    return ir.Unresolved(span=node.span)
            elif op in (ir.CmpOp.MATCH, ir.CmpOp.NOT_MATCH):
                # Trapped here rather than by the enclosing trap: a bad pattern must not cost the
                # modifier diagnostics below.
                with self.trap(node.right.span or node.span):
                    Validator.regex_pattern()(node.right.raw)
                rhs = ir.RegexLit(text=node.right.raw)
            else:
                rhs = self._operand(node.right, node.span)

            # A denied, unknown or conflicting modifier drops itself and every modifier after it,
            # but the comparison survives. Validated here, not left to the emitter, so that it is
            # a collected error rather than a raise out of the compile.
            mods: list[str] = []
            mod_span = node.modifier_span or node.span
            state = CondState()
            for mod in node.modifiers:
                if not self._sandbox_check(mod_span, lambda m=mod: self._sandbox.check_modifier(m.upper())):
                    break
                try:
                    state.add_modifier(mod)
                except SymbolResolutionError as exc:
                    self._report(mod_span, exc)
                    break
                mods.append(mod.upper())

            return ir.Test(ref=ref, op=op, rhs=rhs, modifiers=tuple(mods), span=node.span)
        return ir.Unresolved(span=node.span)

    def _reference(self, name: str, span: ast.Span) -> tuple[ir.Ref, bool]:
        if not name:
            raise SymbolResolutionError("identifier", "Missing or empty identifier text")

        try:
            found = self.symbol_resolver.lookup_condition(name, self._section)
        except SandboxDenialError:
            raise
        except SymbolResolutionError:
            # Only an unqualified name can be a variable; a dotted or qualified one keeps the resolver's error.
            if '.' not in name and ':' not in name:
                error = SymbolResolutionError(
                    "identifier", f"Undefined variable: '{name}'. Variables must be declared in a VARS section."
                )
                if suggestions := self.symbol_resolver.get_variable_suggestions(name, self._section):
                    error.add_symbol_suggestion(suggestions)
                raise error from None
            raise

        self._drain_resolver_warnings(span)

        if found.symbol is not None:
            return ir.VarRef(symbol=found.symbol), False
        return ir.BuiltinRef(key=found.key, entry=found.entry, arg=found.arg), found.existence

    def _assignment(self, node: ast.Assignment) -> ir.Stmt | None:
        with self.trap(node.span):
            value = self._operand(node.value, node.span)
            value_text = ir.value_text(value)
            add = node.operator == "+="
            found = self.symbol_resolver.lookup_operator(node.name, value_text, self._section, add=add)
            self._drain_resolver_warnings(node.span)

            if found.symbol is not None:
                if isinstance(node.value, ast.IdentValue) and (rhs_symbol := self.symbol_resolver.symbol_for(node.value.raw)):
                    value = ir.VarRef(symbol=rhs_symbol)
                return ir.SetVar(symbol=found.symbol, value=value, span=node.span)

            if add:
                variant = ir.OpVariant.ADD
            elif value_text == '""':
                variant = ir.OpVariant.REMOVE
            else:
                variant = ir.OpVariant.SET

            return ir.SetOp(
                key=found.key, entry=found.entry, qualifier=found.qualifier, value=value, variant=variant, span=node.span
            )
        return None

    def _statement_call(self, node: ast.FunctionCall) -> ir.Stmt | None:
        with self.trap(node.span):
            if '::' in node.name:
                raise self.source_map.error_at(node.span, f"unknown procedure '{node.name}': not loaded via 'use'")
            args = [self._value(a, node.span) for a in node.args]
            arg_texts = [ir.value_text(a) for a in args]
            key, entry = self.symbol_resolver.lookup_statement_function(node.name, arg_texts, self._section)
            self._drain_resolver_warnings(node.span)
            return ir.CallOp(key=key, entry=entry, args=tuple(map(self._normalized_arg, args, arg_texts)), span=node.span)
        return None

    @staticmethod
    def _normalized_arg(arg: ir.RValue, text: str) -> ir.RValue:
        """Validators normalize argument text in place; an argument they left alone keeps its resolved form."""
        if text == ir.value_text(arg):
            return arg
        return ir.RawArg(text=text)

    @contextmanager
    def _inlining(self, call: ast.FunctionCall):
        sig = self.procs.get(call.name)
        if call.name in self._proc_stack:
            self._report(call.span, ValueError(f"circular procedure call: {' -> '.join([*self._proc_stack, call.name])}"))
            yield None
            return

        bindings = self._bind(sig, call)
        if bindings is None:
            yield None
            return

        body = tuple(self._substitute(node, bindings) for node in sig.body)
        self._proc_stack.append(call.name)
        try:
            yield body
        finally:
            self._proc_stack.pop()

    def _bind(self, sig: ProcEntry, call: ast.FunctionCall) -> dict[str, str] | None:
        with self.trap(call.span):
            args = [self._binding_text(a, call.span) for a in call.args]
            required = sum(1 for p in sig.params if p.default is None)
            if not (required <= len(args) <= len(sig.params)):
                expected = str(len(sig.params))
                if required < len(sig.params):
                    expected = f"{required}-{len(sig.params)}"
                raise ValueError(f"procedure '{sig.name}': expected {expected} arg(s), got {len(args)}")

            bindings = dict(zip((p.name for p in sig.params), args))
            for param in sig.params[len(args) :]:
                bindings[param.name] = self._default_text(param.default, call.span)
            return bindings
        return None

    def _default_text(self, value: ast.ValueExpr, span: ast.Span) -> str:
        """A string default loses its quotes but, unlike a call argument, is not interpolated."""
        if isinstance(value, ast.LiteralStringValue):
            return value.raw
        return self._binding_text(value, span)

    def _binding_text(self, value, span: ast.Span) -> str:
        """A binding is plain text: a string literal contributes its inner text."""
        if isinstance(value, ast.LiteralStringValue):
            return ir.value_text(self._template(value, span))[1:-1]
        if isinstance(value, ast.ParamRef):
            # _substitute replaced every bound `$param`, so one left here is unbound.
            raise self._unbound_param(value.raw, value.span or span)
        return ir.value_text(self._value(value, span))

    def _substitute(self, node: ast.BodyNode, bindings: dict[str, str]) -> ast.BodyNode:

        def sub_text(text: str) -> str:
            return _PARAM_REF.sub(lambda m: bindings.get(m.group(1), m.group(0)), text)

        def sub_string(value):
            if isinstance(value, ast.LiteralStringValue):
                return ast.LiteralStringValue(raw=sub_text(value.raw), written=value.written or value.raw)
            return value

        def sub_value(value):
            if isinstance(value, ast.ParamRef) and value.raw in bindings:
                # A bare `$param` takes its binding verbatim, so a string argument reaches the operator unquoted.
                return ast.IdentValue(raw=bindings[value.raw])
            # An unbound `$param` stays a ParamRef so the statement using it reports the error, in source order.
            return sub_string(value)

        # replace(), not a rebuild: a rebuilt node silently drops any field it does not name,
        # such as `else_span` or `modifier_span`, and the inlined copy then diagnoses differently.
        match node:
            case ast.Assignment():
                return replace(node, value=sub_value(node.value))
            case ast.FunctionCall() if self.procs.get(node.name) is not None:
                return replace(node, args=tuple(sub_value(a) for a in node.args))
            case ast.FunctionCall():
                # An operator argument keeps its raw `$name` for the validator to reject; only a
                # string argument is substituted.
                return replace(node, args=tuple(map(sub_string, node.args)))
            case ast.IfBlock():
                return replace(
                    node,
                    condition=self._substitute_cond(node.condition, sub_value),
                    body=tuple(self._substitute(b, bindings) for b in node.body),
                    elif_branches=tuple(
                        replace(
                            arm,
                            condition=self._substitute_cond(arm.condition, sub_value),
                            body=tuple(self._substitute(b, bindings) for b in arm.body),
                        )
                        for arm in node.elif_branches
                    ),
                    else_body=tuple(self._substitute(b, bindings) for b in node.else_body),
                )
        return node

    def _substitute_cond(self, node: ast.ConditionExpr, sub_value: Callable) -> ast.ConditionExpr:
        match node:
            case ast.Comparison():
                return replace(node, right=sub_value(node.right))
            case ast.LogicalOp():
                return replace(
                    node, left=self._substitute_cond(node.left, sub_value), right=self._substitute_cond(node.right, sub_value)
                )
            case ast.NotOp():
                return replace(node, operand=self._substitute_cond(node.operand, sub_value))
            case ast.Group():
                return replace(node, inner=self._substitute_cond(node.inner, sub_value))
        return node

    def _value(self, node, span: ast.Span) -> ir.RValue:
        match node:
            case ast.NumberValue() | ast.IdentValue() | ast.BoolValue() | ast.IPValue():
                return ir.RawArg(text=node.raw)
            case ast.LiteralStringValue():
                return self._template(node, span)
            case ast.ParamRef():
                # Unsubstituted: the entry's validator produces the diagnostic.
                return ir.RawArg(text=f"${node.raw}")
            case ast.RegexValue():
                return ir.RegexLit(text=node.raw)
            case ast.SetValue():
                return ir.SetLit(text=node.raw)
            case ast.IpRangeValue():
                return ir.IpRangeLit(text=node.raw)
        raise SymbolResolutionError("value", f"Unsupported value {node!r}")

    def _operand(self, node, span: ast.Span) -> ir.RValue:
        """An assignment or comparison right-hand side, where an unbound `$name` is an error."""
        if isinstance(node, ast.ParamRef):
            raise self._unbound_param(node.raw, node.span or span)
        return self._value(node, span)

    def _source_text(self, node, span: ast.Span) -> str:
        if isinstance(node, ast.LiteralStringValue):
            return f'"{node.raw}"'
        return ir.value_text(self._value(node, span))

    def _template(self, literal: ast.LiteralStringValue, span: ast.Span) -> ir.StrTemplate:
        inner = literal.raw
        parts: list[str | ir.BuiltinRef | ir.VarRef | ir.ModifiedRef] = []
        pos = 0
        for m in RegexPatterns.SUBSTITUTE_PATTERN.finditer(inner):
            if m.start() > pos:
                parts.append(inner[pos : m.start()])
            parts.append(self._template_part(m, span, literal.written or inner))
            pos = m.end()
        if pos < len(inner):
            parts.append(inner[pos:])
        return ir.StrTemplate(parts=tuple(parts))

    def _template_part(self, m, span: ast.Span, inner: str) -> str | ir.BuiltinRef | ir.VarRef | ir.ModifiedRef:
        try:
            return self._template_part_inner(m, span)
        except SandboxDenialError as exc:
            # Unwrapped, so the sandbox message is still set.
            exc.add_note(self._interpolation_note(inner))
            self._report(span, exc)
            failure = exc
        except Exception as exc:
            # Both notes: neither the literal the hole came from nor the section it was read in
            # is recoverable from the span.
            wrapped = RuntimeError(f"symbol error in {{}}: {exc}")
            wrapped.add_note(self._interpolation_note(inner))
            if self._section:
                wrapped.add_note(f"Current section: {self._section.value}")
            self._report(span, wrapped)
            failure = exc
        # A bad hole must not cost the assignment around it.
        return f"{{ERROR: {failure}}}"

    @staticmethod
    def _interpolation_note(inner: str) -> str:
        literal = f'"{inner}"'
        return f"String interpolation context: {literal[:50]}..."

    def _template_part_inner(self, m, span: ast.Span):
        if escaped := m.group("escaped"):
            return _unescape_double_braces(escaped)
        if func := m.group("func"):
            mods = parse_mods(m.group("func_mods"))
            self._check_value_mods(mods, span)
            args = _split_args(m.group("args"))
            key, entry = self.symbol_resolver.lookup_function(func.strip(), args)
            self._drain_resolver_warnings(span)
            return self._with_mods(ir.BuiltinRef.from_function(key, entry, args), mods)
        if var := m.group("var"):
            name, mods = split_interpolation_mods(var)
            self._check_value_mods(mods, span)
            found = self.symbol_resolver.lookup_condition(name, self._section)
            self._drain_resolver_warnings(span)
            if found.symbol is not None:
                return self._with_mods(ir.VarRef(symbol=found.symbol), mods)
            return self._with_mods(ir.BuiltinRef(key=found.key, entry=found.entry, arg=found.arg), mods)
        raise SymbolResolutionError(m.group(0), "Unrecognized substitution format")

    def _check_value_mods(self, mods: list[str], span: ast.Span) -> None:
        for mod in mods:
            if mod not in VALUE_MODIFIERS:
                raise SymbolResolutionError(mod, f"Not a value modifier, expected one of {', '.join(sorted(VALUE_MODIFIERS))}")
            if warning := self._sandbox.check_modifier(mod):
                self._warn(span, warning)

    @staticmethod
    def _with_mods(ref: ir.Ref, mods: list[str]) -> ir.Ref | ir.ModifiedRef:
        if mods:
            return ir.ModifiedRef(ref=ref, mods=tuple(mods))
        return ref

    def _declare_vars(self, node: ast.VarSection) -> ir.VarsBlock:
        noun, label, scope = _VAR_SECTIONS[node.scope]
        if self._section is not None:
            self._report(node.span, ValueError(f"{noun} section must be first in a section"))
            return ir.VarsBlock(comments=(), span=node.span)

        if not self._sandbox_check(node.span, lambda: self._sandbox.check_section(label)):
            return ir.VarsBlock(comments=(), span=node.span)
        if not self._sandbox_check(node.span, lambda: self._sandbox.check_language("variables")):
            return ir.VarsBlock(comments=(), span=node.span)

        comments: list[ir.CommentStmt] = []
        for decl in node.items:
            if isinstance(decl, ast.Comment):
                if self.preserve_comments:
                    comments.append(ir.CommentStmt(text=decl.text, span=decl.span))
                continue
            with self.trap(decl.span, note=self._decl_note(decl)):
                if '.' in decl.name or ':' in decl.name:
                    raise SymbolResolutionError("variable", f"Variable name '{decl.name}' cannot contain '.' or ':' characters")
                self.symbol_resolver.declare_variable(decl.name, decl.type_name, decl.slot, scope)
        return ir.VarsBlock(comments=tuple(comments), span=node.span)

    @staticmethod
    def _decl_note(decl: ast.VarDecl) -> str:
        note = f"Variable declaration: {decl.name}:{decl.type_name}"
        if decl.slot is not None:
            note += f" @{decl.slot}"
        return note
