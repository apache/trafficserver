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

from contextlib import contextmanager
from dataclasses import dataclass
from typing import Any

from hrw4u.debugging import Dbg
from hrw4u.states import SectionType
from hrw4u.common import SystemDefaults
from hrw4u.errors import hrw4u_error, Warning
from hrw4u.sandbox import SandboxConfig, SandboxDenialError


@dataclass(slots=True)
class VisitorState:
    stmt_indent: int = 0
    cond_indent: int = 0
    in_if_block: bool = False
    section_opened: bool = False
    in_elif_mode: bool = False
    current_section: SectionType | None = None


class BaseHRWVisitor:

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None) -> None:
        self.filename = filename
        self.error_collector = error_collector
        self.output: list[str] = []
        self._sandbox = SandboxConfig.empty()

        self._state = VisitorState()
        self._dbg = Dbg(debug)
        self._initialize_visitor()

    def _initialize_visitor(self) -> None:
        pass

    def format_with_indent(self, text: str, indent_level: int) -> str:
        return " " * (indent_level * SystemDefaults.INDENT_SPACES) + text

    @property
    def current_section(self) -> SectionType | None:
        return self._state.current_section

    @current_section.setter
    def current_section(self, section: SectionType | None) -> None:
        self._state.current_section = section

    @property
    def stmt_indent(self) -> int:
        return self._state.stmt_indent

    @stmt_indent.setter
    def stmt_indent(self, level: int) -> None:
        self._state.stmt_indent = level

    @property
    def cond_indent(self) -> int:
        return self._state.cond_indent

    @cond_indent.setter
    def cond_indent(self, level: int) -> None:
        self._state.cond_indent = level

    @property
    def current_indent(self) -> int:
        return self.stmt_indent

    def increment_stmt_indent(self) -> None:
        self._state.stmt_indent += 1

    def decrement_stmt_indent(self) -> None:
        self._state.stmt_indent = max(0, self._state.stmt_indent - 1)

    def increment_cond_indent(self) -> None:
        self._state.cond_indent += 1

    def decrement_cond_indent(self) -> None:
        self._state.cond_indent = max(0, self._state.cond_indent - 1)

    def emit_line(self, text: str, indent_level: int | None = None) -> None:
        if indent_level is None:
            indent_level = self._state.stmt_indent
        self.output.append(self.format_with_indent(text, indent_level))

    def emit_statement(self, statement: str) -> None:
        self.emit_line(statement, self._state.stmt_indent)

    def emit_condition(self, condition: str) -> None:
        self.emit_line(condition, self._state.cond_indent)

    def emit(self, text: str) -> None:
        self.output.append(self.format_with_indent(text, self.current_indent))

    def increase_indent(self) -> None:
        self.increment_stmt_indent()

    def decrease_indent(self) -> None:
        self.decrement_stmt_indent()

    def debug_enter(self, method_name: str, *args: Any) -> None:
        if args:
            arg_strs = [str(arg) for arg in args]
            self._dbg.enter(f"{method_name}: {', '.join(arg_strs)}")
        else:
            self._dbg.enter(method_name)

    def debug_exit(self, method_name: str, result: Any = None) -> None:
        if result is not None:
            self._dbg.exit(f"{method_name} => {result}")
        else:
            self._dbg.exit(method_name)

    def debug(self, message: str) -> None:
        self._dbg(message)

    @property
    def is_debug(self) -> bool:
        return self._dbg.enabled

    def debug_context(self, method_name: str, *args: Any):

        class DebugContext:

            def __init__(self, visitor: BaseHRWVisitor, method: str, arguments: tuple[Any, ...]):
                self.visitor = visitor
                self.method = method
                self.args = arguments

            def __enter__(self):
                self.visitor.debug_enter(self.method, *self.args)
                return self

            def __exit__(self, exc_type, exc_val, exc_tb):
                if exc_type is None:
                    self.visitor.debug_exit(self.method)
                else:
                    self.visitor.debug_exit(f"{self.method} (exception: {exc_type.__name__})")

        return DebugContext(self, method_name, args)

    def _add_sandbox_warning(self, ctx, message: str) -> None:
        """Format and collect a sandbox warning with source context."""
        if self.error_collector:
            self.error_collector.add_warning(Warning.from_ctx(self.filename, ctx, message))
            if self._sandbox.message:
                self.error_collector.set_sandbox_message(self._sandbox.message)

    def trap(self, ctx, *, note: str | None = None):

        class _Trap:

            def __enter__(_):
                return _

            def __exit__(_, exc_type, exc, tb):
                if not exc:
                    return False

                error = hrw4u_error(self.filename, ctx, exc)
                if note and hasattr(error, 'add_note'):
                    error.add_note(note)

                if self.error_collector:
                    self.error_collector.add_error(error)
                    if isinstance(exc, SandboxDenialError) and exc.sandbox_message:
                        self.error_collector.set_sandbox_message(exc.sandbox_message)
                    return True
                else:
                    raise error from exc

        return _Trap()

    @contextmanager
    def stmt_indented(self):
        self.stmt_indent += 1
        try:
            yield
        finally:
            self.stmt_indent -= 1

    @contextmanager
    def cond_indented(self):
        self.cond_indent += 1
        try:
            yield
        finally:
            self.cond_indent -= 1

    def get_final_output(self) -> list[str]:
        self._finalize_output()
        return self.output

    def _finalize_output(self) -> None:
        pass

    def handle_error(self, exc: Exception) -> None:
        if self.error_collector:
            self.error_collector.add_error(exc)
        else:
            raise exc

    def _apply_with_modifiers(self, expr: str, state) -> str:
        if hasattr(state, 'to_with_modifiers'):
            with_mods = state.to_with_modifiers()
            return f"{expr} with {','.join(with_mods)}" if with_mods else expr
        return expr

    def _normalize_empty_string_condition(self, term: str) -> str:
        if term.endswith(' != ""'):
            return term.replace(' != ""', '')
        elif term.endswith(' == ""'):
            return f"!{term.replace(' == \"\"', '')}"
        return term

    def _build_condition_connector(self, state, is_last_term: bool = False) -> str:
        if hasattr(state, 'and_or') and state.and_or and not is_last_term:
            return "||"
        return "&&"

    def _reconstruct_redirect_args(self, args: list[str]) -> list[str]:
        if len(args) <= 1:
            return args
        url = "".join(a[1:-1] if a.startswith('"') and a.endswith('"') else a for a in args[1:])
        return [args[0], url]

    def _parse_op_tails(self, node, ctx=None) -> tuple[list[str], object, object]:
        from hrw4u.states import CondState, OperatorState, ModifierType

        args: list[str] = []
        cond_state = CondState()
        op_state = OperatorState()

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
                        if ctx and hasattr(self, 'trap'):
                            with self.trap(ctx):
                                raise Exception(f"Unknown modifier: {flag_text}")
                        else:
                            raise Exception(f"Unknown modifier: {flag_text}")
                    sandbox = self._sandbox
                    if sandbox is not None:
                        warning = sandbox.check_modifier(flag_text)
                        if warning and ctx:
                            self._add_sandbox_warning(ctx, warning)
                continue

            for kind in ("IDENT", "NUMBER", "STRING", "PERCENT_BLOCK", "COMPLEX_STRING"):
                tok = getattr(tail, kind, None)
                if tok and tok():
                    args.append(tok().getText())
                    break
            else:
                txt = tail.getText()
                if txt:
                    args.append(txt)

        return args, op_state, cond_state
