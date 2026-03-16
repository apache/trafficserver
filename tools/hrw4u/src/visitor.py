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
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from antlr4 import InputStream, CommonTokenStream
from antlr4.error.ErrorStrategy import BailErrorStrategy

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.symbols import SymbolResolver, SymbolResolutionError
from hrw4u.errors import hrw4u_error, Hrw4uSyntaxError, ThrowingErrorListener
from hrw4u.states import CondState, SectionType
from hrw4u.common import RegexPatterns, SystemDefaults
from hrw4u.visitor_base import BaseHRWVisitor
from hrw4u.validation import Validator
from hrw4u.procedures import resolve_use_path
from hrw4u.sandbox import SandboxConfig, SandboxDenialError

_regex_validator = Validator.regex_pattern()


@dataclass(slots=True)
class ProcParam:
    name: str
    default_ctx: Any  # value parse tree node, or None


@dataclass(slots=True)
class ProcSig:
    qualified_name: str
    params: list[ProcParam]
    body_ctx: Any  # block parse tree node
    source_file: str
    source_text: str  # full text of source file (for flatten)


@dataclass(slots=True)
class QueuedItem:
    text: str
    state: CondState
    indent: int


class HRW4UVisitor(hrw4uVisitor, BaseHRWVisitor):
    _SUBSTITUTE_PATTERN = RegexPatterns.SUBSTITUTE_PATTERN
    _PARAM_REF_PATTERN = re.compile(r'\$([a-zA-Z_][a-zA-Z0-9_-]*)')

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None,
            preserve_comments: bool = True,
            proc_search_paths: list[Path] | None = None,
            sandbox: SandboxConfig | None = None) -> None:
        super().__init__(filename, debug, error_collector)

        self._cond_state = CondState()
        self._queued: QueuedItem | None = None
        self.preserve_comments = preserve_comments
        self._sandbox = sandbox or SandboxConfig.empty()

        self.symbol_resolver = SymbolResolver(debug, sandbox=self._sandbox, dbg=self._dbg)

        self._proc_registry: dict[str, ProcSig] = {}
        self._proc_loaded: set[str] = set()
        self._proc_bindings: dict[str, str] = {}
        self._proc_call_stack: list[str] = []
        self._proc_search_paths: list[Path] = list(proc_search_paths) if proc_search_paths else []
        self._source_text: str = ""

    def _sandbox_check(self, ctx, check_fn) -> bool:
        """Run a sandbox check, trapping any denial error into the error collector.

        Returns True if the check passed (or warned), False if denied.
        """
        try:
            warning = check_fn()
            if warning:
                self._add_sandbox_warning(ctx, warning)
            return True
        except SandboxDenialError:
            with self.trap(ctx):
                raise
            return False

    def _drain_resolver_warnings(self, ctx) -> None:
        """Drain any warnings accumulated in the symbol resolver."""
        for warning in self.symbol_resolver.drain_warnings():
            self._add_sandbox_warning(ctx, warning)

    @lru_cache(maxsize=256)
    def _cached_symbol_resolution(self, symbol_text: str, section_name: str) -> tuple[str, bool]:
        try:
            section = SectionType(section_name)
            return self.symbol_resolver.resolve_condition(symbol_text, section)
        except (ValueError, SymbolResolutionError):
            return symbol_text, False

    @lru_cache(maxsize=128)
    def _cached_hook_mapping(self, section_name: str) -> str:
        return self.symbol_resolver.map_hook(section_name)

    def _make_condition(self, cond_text: str, last: bool = False, negate: bool = False) -> str:
        self._dbg(f"make_condition: {cond_text} last={last} negate={negate}")
        self._cond_state.not_ ^= negate
        self._cond_state.last = last
        return f"cond {cond_text}"

    def _queue_condition(self, text: str) -> None:
        self.debug(f"queue cond: {text}  state={self._cond_state.to_list()}")
        self._queued = QueuedItem(text=text, state=self._cond_state.copy(), indent=self.cond_indent)
        self._cond_state.reset()

    def _flush_condition(self) -> None:
        if self._queued:
            mods = self._queued.state.to_list()
            self.debug(f"flush cond: {self._queued.text} state={mods} indent={self._queued.indent}")
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
        inner = s[1:-1]

        if self._proc_bindings:
            inner = self._PARAM_REF_PATTERN.sub(lambda m: self._proc_bindings.get(m.group(1), m.group(0)), inner)

        def repl(m: re.Match) -> str:
            try:
                if m.group("escaped"):
                    return m.group("escaped")
                if m.group("func"):
                    func_name = m.group("func").strip()
                    arg_str = m.group("args").strip()
                    args = self._parse_function_args(arg_str) if arg_str else []
                    replacement = self.symbol_resolver.resolve_function(func_name, args, strip_quotes=False)
                    self._drain_resolver_warnings(ctx)
                    self.debug(f"substitute: {{{func_name}({arg_str})}} -> {replacement}")
                    return replacement
                if m.group("var"):
                    var_name = m.group("var").strip()
                    replacement, _ = self.symbol_resolver.resolve_condition(var_name, self.current_section)
                    self._drain_resolver_warnings(ctx)
                    self.debug(f"substitute: {{{var_name}}} -> {replacement}")
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
        if not name:
            raise SymbolResolutionError("identifier", "Missing or empty identifier text")

        if entry := self.symbol_resolver.symbol_for(name):
            return entry.as_cond(), False

        symbol, default_expr = self._cached_symbol_resolution(name, self.current_section.value)

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

    def _get_value_text(self, val_ctx) -> str:
        if val_ctx.paramRef():
            name = val_ctx.paramRef().IDENT().getText()
            if name not in self._proc_bindings:
                try:
                    source_line = val_ctx.start.getInputStream().strdata.splitlines()[val_ctx.start.line - 1]
                except Exception:
                    source_line = ""
                raise Hrw4uSyntaxError(
                    self.filename, val_ctx.start.line, val_ctx.start.column, f"'${name}' used outside procedure context",
                    source_line)
            return self._proc_bindings[name]
        return val_ctx.getText()

    def _collect_proc_params(self, param_list_ctx) -> list[ProcParam]:
        return [ProcParam(name=p.IDENT().getText(), default_ctx=p.value() if p.value() else None) for p in param_list_ctx.param()]

    def _load_proc_file(self, path: Path, load_stack: list[str], use_spec: str | None = None) -> None:
        """Parse a procedure file and register its declarations."""
        abs_path = str(path.resolve())
        if abs_path in self._proc_loaded:
            return
        if abs_path in load_stack:
            cycle = ' -> '.join([*load_stack, abs_path])
            raise Hrw4uSyntaxError(str(path), 1, 0, f"circular use dependency: {cycle}", "")

        # Derive expected namespace prefix from the use spec.
        # 'Apple::Common' → 'Apple::', 'Apple::Simple::All' → 'Apple::Simple::'
        expected_ns = None
        if use_spec and '::' in use_spec:
            expected_ns = use_spec[:use_spec.rindex('::') + 2]

        text = path.read_text(encoding='utf-8')
        listener = ThrowingErrorListener(filename=str(path))

        lexer = hrw4uLexer(InputStream(text))
        lexer.removeErrorListeners()
        lexer.addErrorListener(listener)

        stream = CommonTokenStream(lexer)
        parser = hrw4uParser(stream)
        parser.removeErrorListeners()
        parser.addErrorListener(listener)
        parser.errorHandler = BailErrorStrategy()
        tree = parser.program()

        new_stack = [*load_stack, abs_path]
        found_proc = False

        for item in tree.programItem():
            if item.useDirective():
                spec = item.useDirective().QUALIFIED_IDENT().getText()
                sub_path = resolve_use_path(spec, self._proc_search_paths)
                if sub_path is None:
                    raise Hrw4uSyntaxError(
                        str(path),
                        item.useDirective().start.line, 0, f"use '{spec}': file not found in procedures path", "")
                self._load_proc_file(sub_path, new_stack, use_spec=spec)
                found_proc = True
            elif item.procedureDecl():
                ctx = item.procedureDecl()
                name = ctx.QUALIFIED_IDENT().getText()
                if '::' not in name:
                    raise Hrw4uSyntaxError(
                        str(path), ctx.start.line, ctx.start.column, f"procedure name '{name}' must be qualified (e.g. 'ns::name')",
                        "")
                if expected_ns and not name.startswith(expected_ns):
                    raise Hrw4uSyntaxError(
                        str(path), ctx.start.line, ctx.start.column,
                        f"procedure '{name}' does not match namespace '{expected_ns[:-2]}' "
                        f"(expected from 'use {use_spec}')", "")
                if name in self._proc_registry:
                    existing = self._proc_registry[name]
                    raise Hrw4uSyntaxError(
                        str(path), ctx.start.line, 0, f"procedure '{name}' already declared in {existing.source_file}", "")
                params = self._collect_proc_params(ctx.paramList()) if ctx.paramList() else []
                self._proc_registry[name] = ProcSig(name, params, ctx.block(), str(path), text)
                found_proc = True

        if not found_proc:
            raise Hrw4uSyntaxError(str(path), 1, 0, f"no 'procedure' declarations found in {path.name}", "")

        self._proc_loaded.add(abs_path)

    def _visit_block_items(self, block_ctx) -> None:
        """Visit a block's items at the current indent level (no extra indent added)."""
        for item in block_ctx.blockItem():
            if item.statement():
                self.visit(item.statement())
            elif item.conditional():
                self.emit_statement("if")
                saved = self.stmt_indent, self.cond_indent
                self.stmt_indent += 1
                self.cond_indent = self.stmt_indent
                self.visit(item.conditional())
                self.stmt_indent, self.cond_indent = saved
                self.emit_statement("endif")
            elif item.commentLine() and self.preserve_comments:
                self.visit(item.commentLine())

    def _bind_proc_args(self, sig: ProcSig, call_ctx) -> dict[str, str]:
        """Resolve call arguments against a procedure signature, returning bindings."""
        call_args: list[str] = []
        if call_ctx.argumentList():
            for val_ctx in call_ctx.argumentList().value():
                text = self._get_value_text(val_ctx)
                if text.startswith('"') and text.endswith('"'):
                    text = self._substitute_strings(text, call_ctx)[1:-1]
                call_args.append(text)

        required = sum(1 for p in sig.params if p.default_ctx is None)
        if not (required <= len(call_args) <= len(sig.params)):
            expected = f"{required}-{len(sig.params)}" if required < len(sig.params) else str(len(sig.params))
            raise Hrw4uSyntaxError(
                self.filename, call_ctx.start.line, call_ctx.start.column,
                f"procedure '{sig.qualified_name}': expected {expected} arg(s), got {len(call_args)}", "")

        bindings: dict[str, str] = {}
        for i, param in enumerate(sig.params):
            if i < len(call_args):
                bindings[param.name] = call_args[i]
            else:
                default = self._get_value_text(param.default_ctx)
                if default.startswith('"') and default.endswith('"'):
                    default = default[1:-1]
                bindings[param.name] = default

        return bindings

    @contextmanager
    def _proc_context(self, sig: ProcSig, bindings: dict[str, str]):
        """Context manager that saves/restores procedure expansion state."""
        saved_bindings = self._proc_bindings
        saved_stack = self._proc_call_stack
        saved_filename = self.filename

        self._proc_bindings = bindings
        self._proc_call_stack = [*saved_stack, sig.qualified_name]
        self.filename = sig.source_file
        try:
            yield
        finally:
            self._proc_bindings = saved_bindings
            self._proc_call_stack = saved_stack
            self.filename = saved_filename

    def _expand_proc_as_section_body(self, block_ctx, hook: str, in_statement_block: bool) -> bool:
        """Expand a procedure body using section-body semantics (hook re-emission).

        Returns the final in_statement_block state.
        """
        items = block_ctx.blockItem()
        is_first = not in_statement_block

        for idx, item in enumerate(items):
            is_conditional = item.conditional() is not None
            is_comment = item.commentLine() is not None
            proc_info = self._get_proc_call_info(item)

            if is_comment:
                if self.preserve_comments:
                    self.visit(item.commentLine())
            elif proc_info:
                _, call_ctx = proc_info
                in_statement_block = self._section_expand_proc_call(call_ctx, hook, in_statement_block, is_first and idx == 0)
            elif is_conditional or not in_statement_block:
                if not (is_first and idx == 0):
                    self._flush_condition()
                    self.output.append("")

                self._emit_section_header(hook, [])

                if is_conditional:
                    self.visit(item)
                    in_statement_block = False
                else:
                    in_statement_block = True
                    with self.stmt_indented():
                        self.visit(item.statement())
            else:
                with self.stmt_indented():
                    self.visit(item.statement())

        return in_statement_block

    def _section_expand_proc_call(self, call_ctx, hook: str, in_statement_block: bool, is_first_item: bool) -> bool:
        """Expand a procedure call within a section body context. Returns in_statement_block."""
        name = call_ctx.funcName.text
        sig = self._proc_registry[name]
        bindings = self._bind_proc_args(sig, call_ctx)

        with self._proc_context(sig, bindings):
            return self._expand_proc_as_section_body(sig.body_ctx, hook, in_statement_block)

    def _get_proc_call_info(self, item) -> tuple[ProcSig, Any] | None:
        """If item (sectionBody or blockItem) is a procedure call, return (sig, call_ctx)."""
        stmt = item.statement()
        if stmt and stmt.functionCall():
            func_name = stmt.functionCall().funcName.text
            sig = self._proc_registry.get(func_name)
            if sig:
                return sig, stmt.functionCall()
        return None

    def _expand_procedure_call(self, call_ctx) -> None:
        """Expand a procedure call inline at the current indent level."""
        name = call_ctx.funcName.text
        sig = self._proc_registry.get(name)

        if sig is None:
            raise Hrw4uSyntaxError(
                self.filename, call_ctx.start.line, call_ctx.start.column, f"unknown procedure '{name}': not loaded via 'use'", "")

        if name in self._proc_call_stack:
            cycle = ' -> '.join([*self._proc_call_stack, name])
            raise Hrw4uSyntaxError(
                self.filename, call_ctx.start.line, call_ctx.start.column, f"circular procedure call: {cycle}", "")

        bindings = self._bind_proc_args(sig, call_ctx)

        with self._proc_context(sig, bindings):
            self._visit_block_items(sig.body_ctx)

    @staticmethod
    def _get_source_text(ctx, source_text: str) -> str:
        """Extract original source text for a parse tree node."""
        return source_text[ctx.start.start:ctx.stop.stop + 1]

    def _flatten_substitute_params(self, text: str, bindings: dict[str, str]) -> str:
        """Replace $param references in source text with bound values."""
        if not bindings:
            return text
        return self._PARAM_REF_PATTERN.sub(lambda m: bindings.get(m.group(1), m.group(0)), text)

    def _flatten_reindent(self, text: str, indent: str, source_indent: str | None = None) -> list[str]:
        """Re-indent text: replace source indentation with target indent, preserving relative nesting.

        If source_indent is None, it is auto-detected from the first non-empty line.
        """
        lines: list[str] = []

        for line in text.splitlines():
            stripped = line.strip()
            if not stripped:
                lines.append("")
                continue

            if source_indent is None:
                source_indent = line[:len(line) - len(line.lstrip())]

            if line.startswith(source_indent):
                lines.append(f"{indent}{line[len(source_indent):]}")
            else:
                lines.append(f"{indent}{stripped}")

        return lines

    @staticmethod
    def _source_indent_at(ctx, source_text: str) -> str:
        """Detect the source indentation of a parse tree node from its position in source text."""
        start = ctx.start.start
        line_start = source_text.rfind('\n', 0, start)
        line_start = 0 if line_start == -1 else line_start + 1
        prefix = source_text[line_start:start]
        return prefix if prefix.isspace() or not prefix else ""

    def _has_proc_calls(self, ctx) -> bool:
        """Check if a block or conditional contains any procedure calls (recursively)."""
        if hasattr(ctx, 'blockItem'):
            for item in ctx.blockItem():
                if self._get_proc_call_info(item):
                    return True
                if item.conditional() and self._has_proc_calls(item.conditional()):
                    return True
            return False

        if self._has_proc_calls(ctx.ifStatement().block()):
            return True
        for elif_ctx in ctx.elifClause():
            if self._has_proc_calls(elif_ctx.block()):
                return True
        if ctx.elseClause() and self._has_proc_calls(ctx.elseClause().block()):
            return True
        return False

    def _flatten_items(self, items, indent: str, source_text: str, bindings: dict[str, str] | None = None) -> list[str]:
        """Flatten a list of sectionBody or blockItem nodes, expanding procedure calls."""
        if bindings is None:
            bindings = {}
        lines: list[str] = []

        for item in items:
            if item.commentLine() is not None:
                if self.preserve_comments:
                    comment_text = self._get_source_text(item.commentLine(), source_text)
                    lines.extend(self._flatten_reindent(comment_text, indent))
                continue

            proc_info = self._get_proc_call_info(item)
            if proc_info:
                sig, call_ctx = proc_info
                nested_bindings = self._bind_proc_args(sig, call_ctx)
                lines.extend(self._flatten_items(sig.body_ctx.blockItem(), indent, sig.source_text, nested_bindings))
                continue

            if item.conditional() and self._has_proc_calls(item.conditional()):
                lines.extend(self._flatten_conditional(item.conditional(), indent, source_text, bindings))
                continue

            item_text = self._get_source_text(item, source_text)
            item_text = self._flatten_substitute_params(item_text, bindings)
            source_indent = self._source_indent_at(item, source_text)
            lines.extend(self._flatten_reindent(item_text, indent, source_indent))

        return lines

    def _flatten_conditional(self, cond_ctx, indent: str, source_text: str, bindings: dict[str, str]) -> list[str]:
        """Flatten a conditional block, expanding proc calls within its branches."""
        lines: list[str] = []
        inner_indent = indent + (" " * SystemDefaults.INDENT_SPACES)

        if_ctx = cond_ctx.ifStatement()
        cond_text = self._get_source_text(if_ctx.condition(), source_text)
        cond_text = self._flatten_substitute_params(cond_text, bindings)
        lines.append(f"{indent}if {cond_text.strip()} {{")
        lines.extend(self._flatten_items(if_ctx.block().blockItem(), inner_indent, source_text, bindings))

        for elif_ctx in cond_ctx.elifClause():
            elif_cond = self._get_source_text(elif_ctx.condition(), source_text)
            elif_cond = self._flatten_substitute_params(elif_cond, bindings)
            lines.append(f"{indent}}} elif {elif_cond.strip()} {{")
            lines.extend(self._flatten_items(elif_ctx.block().blockItem(), inner_indent, source_text, bindings))

        if cond_ctx.elseClause():
            lines.append(f"{indent}}} else {{")
            lines.extend(self._flatten_items(cond_ctx.elseClause().block().blockItem(), inner_indent, source_text, bindings))

        lines.append(f"{indent}}}")
        return lines

    def flatten(self, ctx, source_text: str = "") -> list[str]:
        """Flatten procedures: expand all procedure calls inline and output self-contained HRW4U."""
        if not source_text:
            source_text = ctx.start.source[1].getText(0, ctx.start.source[1].size - 1)
        self._source_text = source_text
        indent = " " * SystemDefaults.INDENT_SPACES

        # Phase 1: Load all procedures (use directives + local procedure declarations)
        for item in ctx.programItem():
            if item.useDirective():
                with self.trap(item.useDirective()):
                    self.visitUseDirective(item.useDirective())
            elif item.procedureDecl():
                with self.trap(item.procedureDecl()):
                    self.visitProcedureDecl(item.procedureDecl())

        # Phase 2: Emit flattened output
        output: list[str] = []
        program_items = ctx.programItem()

        for idx, item in enumerate(program_items):
            if item.useDirective() or item.procedureDecl():
                continue

            if item.commentLine() and self.preserve_comments:
                comment_text = item.commentLine().COMMENT().getText()
                output.append(comment_text)
                continue

            if item.section():
                section_ctx = item.section()

                if section_ctx.varSection():
                    var_text = self._get_source_text(section_ctx.varSection(), self._source_text)
                    output.append(var_text)
                    continue

                section_name = section_ctx.name.text
                output.append(f"{section_name} {{")

                body_lines = self._flatten_items(section_ctx.sectionBody(), indent, self._source_text)
                output.extend(body_lines)
                output.append("}")

                remaining = program_items[idx + 1:]
                if any(r.section() for r in remaining):
                    output.append("")

        return output

    def visitUseDirective(self, ctx) -> None:
        spec = ctx.QUALIFIED_IDENT().getText()
        if not self._proc_search_paths:
            raise Hrw4uSyntaxError(
                self.filename, ctx.start.line, ctx.start.column, "use directive requires --procedures-path to be set", "")
        path = resolve_use_path(spec, self._proc_search_paths)
        if path is None:
            raise Hrw4uSyntaxError(
                self.filename, ctx.start.line, ctx.start.column, f"use '{spec}': file not found in procedures path", "")
        self._load_proc_file(path, [], use_spec=spec)

    def visitProcedureDecl(self, ctx) -> None:
        name = ctx.QUALIFIED_IDENT().getText()
        if '::' not in name:
            raise Hrw4uSyntaxError(
                self.filename, ctx.start.line, ctx.start.column, f"procedure name '{name}' must be qualified (e.g. 'ns::name')", "")
        if name in self._proc_registry:
            existing = self._proc_registry[name]
            raise Hrw4uSyntaxError(
                self.filename, ctx.start.line, ctx.start.column, f"procedure '{name}' already declared in {existing.source_file}",
                "")
        params = self._collect_proc_params(ctx.paramList()) if ctx.paramList() else []
        seen_default = False
        for p in params:
            if p.default_ctx is None and seen_default:
                raise Hrw4uSyntaxError(
                    self.filename, ctx.start.line, ctx.start.column,
                    f"procedure '{name}': required parameter '${p.name}' must not follow an optional parameter", "")
            if p.default_ctx is not None:
                seen_default = True
        self._proc_registry[name] = ProcSig(name, params, ctx.block(), self.filename, self._source_text)

    def visitProgram(self, ctx) -> list[str]:
        with self.debug_context("visitProgram"):
            seen_sections = False
            program_items = ctx.programItem()
            for idx, item in enumerate(program_items):
                start_length = len(self.output)
                if item.useDirective():
                    if seen_sections:
                        error = hrw4u_error(
                            self.filename, item.useDirective(),
                            ValueError("'use' directives must appear before any section blocks"))
                        if self.error_collector:
                            self.error_collector.add_error(error)
                            continue
                        raise error
                    with self.trap(item.useDirective()):
                        self.visitUseDirective(item.useDirective())
                elif item.procedureDecl():
                    if seen_sections:
                        error = hrw4u_error(
                            self.filename, item.procedureDecl(),
                            ValueError("'procedure' declarations must appear before any section blocks"))
                        if self.error_collector:
                            self.error_collector.add_error(error)
                            continue
                        raise error
                    with self.trap(item.procedureDecl()):
                        self.visitProcedureDecl(item.procedureDecl())
                elif item.section():
                    seen_sections = True
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
        warning = self._sandbox.check_section(section_name)
        if warning:
            self._add_sandbox_warning(ctx, warning)

        try:
            self.current_section = SectionType(section_name)
        except ValueError:
            valid_sections = [s.value for s in SectionType]
            raise ValueError(f"Invalid section name: '{section_name}'. Valid sections: {', '.join(valid_sections)}")

        hook = self._cached_hook_mapping(section_name)
        self.debug(f"`{section_name}' -> `{hook}'")
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
            proc_info = self._get_proc_call_info(body)

            if is_comment:
                if self.preserve_comments:
                    if not first_hook_emitted:
                        pending_leading_comments.append(body)
                    else:
                        self.visit(body)
            elif proc_info:
                if not first_hook_emitted:
                    first_hook_emitted = True
                    for comment in pending_leading_comments:
                        self.visit(comment)
                    pending_leading_comments = []
                _, call_ctx = proc_info
                in_statement_block = self._section_expand_proc_call(call_ctx, hook, in_statement_block, idx == 0)
            elif is_conditional or not in_statement_block:
                if first_hook_emitted:
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
            if not self._sandbox_check(ctx, lambda: self._sandbox.check_section("VARS")):
                return
            if not self._sandbox_check(ctx, lambda: self._sandbox.check_language("variables")):
                return
            self.visit(ctx.variables())

    def visitCommentLine(self, ctx) -> None:
        if not self.preserve_comments:
            return
        with self.debug_context("visitCommentLine"):
            comment_text = ctx.COMMENT().getText()
            self.debug(f"preserving comment: {comment_text}")
            self.output.append(comment_text)

    def visitStatement(self, ctx) -> None:
        with self.debug_context("visitStatement"), self.trap(ctx):
            match ctx:
                case _ if ctx.BREAK():
                    warning = self._sandbox.check_language("break")
                    if warning:
                        self._add_sandbox_warning(ctx, warning)
                    self._dbg("BREAK")
                    self.emit_statement("no-op [L]")
                    return

                case _ if ctx.functionCall():
                    func, args = self._parse_function_call(ctx.functionCall())
                    if func in self._proc_registry:
                        self._expand_procedure_call(ctx.functionCall())
                        return
                    if '::' in func:
                        raise Hrw4uSyntaxError(
                            self.filename,
                            ctx.functionCall().start.line,
                            ctx.functionCall().start.column, f"unknown procedure '{func}': not loaded via 'use'", "")
                    subst_args = [
                        self._substitute_strings(arg, ctx) if arg.startswith('"') and arg.endswith('"') else arg for arg in args
                    ]
                    symbol = self.symbol_resolver.resolve_statement_func(func, subst_args, self.current_section)
                    self._drain_resolver_warnings(ctx)
                    self.emit_statement(symbol)
                    return

                case _ if ctx.EQUAL():
                    if ctx.lhs is None:
                        raise SymbolResolutionError("assignment", "Missing left-hand side in assignment")
                    lhs = ctx.lhs.text
                    rhs = self._get_value_text(ctx.value())
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    self._dbg(f"assignment: {lhs} = {rhs}")
                    out = self.symbol_resolver.resolve_assignment(lhs, rhs, self.current_section)
                    self._drain_resolver_warnings(ctx)
                    self.emit_statement(out)
                    return

                case _ if ctx.PLUSEQUAL():
                    if ctx.lhs is None:
                        raise SymbolResolutionError("assignment", "Missing left-hand side in += assignment")
                    lhs = ctx.lhs.text
                    rhs = self._get_value_text(ctx.value())
                    if rhs.startswith('"') and rhs.endswith('"'):
                        rhs = self._substitute_strings(rhs, ctx)
                    self._dbg(f"add assignment: {lhs} += {rhs}")
                    out = self.symbol_resolver.resolve_add_assignment(lhs, rhs, self.current_section)
                    self._drain_resolver_warnings(ctx)
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
            if not self._sandbox_check(ctx, lambda: self._sandbox.check_language("else")):
                return
            self.emit_condition("else", final=True)
            self.visit(ctx.block())

    def visitElifClause(self, ctx) -> None:
        with self.debug_context("visitElifClause"):
            if not self._sandbox_check(ctx, lambda: self._sandbox.check_language("elif")):
                return
            self.emit_condition("elif", final=True)
            with self.stmt_indented(), self.cond_indented():
                self.visit(ctx.condition())
                self.visit(ctx.block())

    def visitBlock(self, ctx) -> None:
        with self.debug_context("visitBlock"):
            with self.stmt_indented():
                self._visit_block_items(ctx)

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
                    self._drain_resolver_warnings(ctx)
                else:
                    lhs = self.visitFunctionCall(comp.functionCall())
            if not lhs:
                return
            operator = ctx.getChild(1)

            # Detect negation: '!=' and '!~' are single tokens (NEQ, NOT_TILDE),
            # but '!in' is two separate tokens ('!' + IN).
            if operator.getText() == '!':
                negate = True
            else:
                negate = operator.symbol.type in (hrw4uParser.NEQ, hrw4uParser.NOT_TILDE)

            match ctx:
                case _ if ctx.value():
                    rhs = self._get_value_text(ctx.value())
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
                        regex_expr = "/.*/'"
                    cond_txt = f"{lhs} {regex_expr}"

                case _ if ctx.iprange():
                    cond_txt = f"{lhs} {ctx.iprange().getText()}"

                case _ if ctx.set_():
                    if not self._sandbox_check(ctx, lambda: self._sandbox.check_language("in")):
                        return
                    inner = ctx.set_().getText()[1:-1]
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
                warning = self._sandbox.check_modifier(mod)
                if warning:
                    self._add_sandbox_warning(ctx, warning)
                self._cond_state.add_modifier(mod)
            except Exception as exc:
                with self.trap(ctx):
                    raise exc
                return

    def visitFunctionCall(self, ctx) -> str:
        with self.trap(ctx):
            func, raw_args = self._parse_function_call(ctx)
            self._dbg(f"function: {func}({', '.join(raw_args)})")
            result = self.symbol_resolver.resolve_function(func, raw_args, strip_quotes=True)
            self._drain_resolver_warnings(ctx)
            return result
        return "ERROR"

    def emit_condition(self, text: str, *, final: bool = False) -> None:
        if final:
            self.output.append(self.format_with_indent(text, self.cond_indent))
        else:
            if self._queued:
                self._flush_condition()
            self._queue_condition(text)

    def emit_separator(self) -> None:
        self.output.append("")

    def emit_statement(self, line: str) -> None:
        self._flush_condition()
        super().emit_statement(line)

    def _end_lhs_then_emit_rhs(self, set_and_or: bool, rhs_emitter) -> None:
        if self._queued:
            self._queued.state.and_or = set_and_or
            if not set_and_or:
                self._queued.indent = self.cond_indent
        self._flush_condition()
        rhs_emitter()

    def emit_expression(self, ctx, *, nested: bool = False, last: bool = False, grouped: bool = False) -> None:
        with self.debug_context("emit_expression"):
            if ctx.OR():
                if not self._sandbox_check(ctx, lambda: self._sandbox.check_modifier("OR")):
                    return
                self.debug("`OR' detected")
                if grouped:
                    self.debug("GROUP-START")
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
                if not self._sandbox_check(ctx, lambda: self._sandbox.check_modifier("AND")):
                    return
                self.debug("`AND' detected")
                self.emit_term(ctx.term(), last=False)
                self._end_lhs_then_emit_rhs(False, lambda: self.emit_factor(ctx.factor(), last=last))
            else:
                self.emit_factor(ctx.factor(), last=last)

    def emit_factor(self, ctx, *, last: bool = False) -> None:
        with self.debug_context("emit_factor"), self.trap(ctx):
            match ctx:
                case _ if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
                    self._dbg("`NOT' detected")
                    child = ctx.getChild(1)
                    if child.LPAREN():
                        self._dbg("GROUP-START (negated)")
                        self.emit_condition("cond %{GROUP}", final=True)
                        with self.cond_indented():
                            self.emit_expression(child.expression(), nested=False, last=True, grouped=False)
                        self._cond_state.last = last
                        self._cond_state.not_ = True
                        self.emit_condition("cond %{GROUP:END}")
                    else:
                        self._cond_state.not_ = True
                        self.emit_factor(child, last=last)

                case _ if ctx.LPAREN():
                    self._dbg("GROUP-START")
                    self.emit_condition("cond %{GROUP}", final=True)
                    with self.cond_indented():
                        # grouped=False: GROUP is already open; no second wrap for OR.
                        self.emit_expression(ctx.expression(), nested=False, last=True, grouped=False)
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
                    self._drain_resolver_warnings(ctx)

                    if default_expr:
                        cond_txt = f"{symbol} =\"\""
                        negate = not self._cond_state.not_
                    else:
                        cond_txt = symbol
                        negate = self._cond_state.not_

                    cond_txt = self._normalize_empty_string_condition(cond_txt)
                    cond_txt = self._apply_with_modifiers(cond_txt, self._cond_state)

                    self._cond_state.not_ = False
                    self._dbg(f"{'implicit' if default_expr else 'explicit'} comparison: {cond_txt} negate={negate}")
                    cond = self._make_condition(cond_txt, last=last, negate=negate)
                    self.emit_condition(cond)
