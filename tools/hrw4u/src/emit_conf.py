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

import hrw4u.sema_nodes as ir
from hrw4u.common import HeaderOperations, SystemDefaults
from hrw4u.states import CondState
from hrw4u.types import MagicStrings


class ConfEmitter:
    """Renders a resolved tree as header_rewrite configuration lines."""

    def __init__(self) -> None:
        # No comment policy here: sema only builds a CommentStmt when it was asked to keep
        # comments, so a second flag could only disagree with the tree it is handed.
        self._out: list[str] = []
        self._state = CondState()
        self._queued: tuple[str, CondState, int] | None = None

    def emit(self, program: ir.Program) -> list[str]:
        self._out = []
        blocks = [i for i in program.items if isinstance(i, (ir.ResolvedSection, ir.VarsBlock))]
        last_block = None
        if blocks:
            last_block = blocks[-1]

        for item in program.items:
            if isinstance(item, ir.CommentStmt):
                self._out.append(item.text)
                continue
            before = len(self._out)
            if isinstance(item, ir.VarsBlock):
                self._out.extend(c.text for c in item.comments)
            else:
                self._emit_section(item)
            if len(self._out) > before and item is not last_block:
                self._out.append("")

        while self._out and self._out[-1] == "":
            self._out.pop()
        return self._out

    def _emit_section(self, section: ir.ResolvedSection) -> None:
        header = f"cond %{{{section.hook}}} [AND]"
        in_run = False
        hooked = False
        pending: list[str] = []

        for stmt in section.body:
            if isinstance(stmt, ir.CommentStmt):
                # Unindented even inside a statement run; only a block indents its comments.
                if hooked:
                    self._out.append(stmt.text)
                else:
                    pending.append(stmt.text)
                continue

            if isinstance(stmt, ir.Inlined):
                # The call itself frames nothing: leading comments go out ahead of whatever
                # hook line the body opens.
                if not hooked:
                    hooked = True
                    self._out.extend(pending)
                    pending = []
                in_run = self._emit_inlined(stmt, header, in_run)
            elif isinstance(stmt, ir.If) or not in_run:
                if hooked:
                    self._out.append("")
                self._out.append(header)
                if not hooked:
                    hooked = True
                    self._out.extend(pending)
                    pending = []
                in_run = self._emit_framed(stmt)
            else:
                self._emit_stmt(stmt, 1)

        if not hooked and pending:
            self._out.append(header)
            self._out.extend(pending)

    def _emit_inlined(self, inlined: ir.Inlined, header: str, in_run: bool) -> bool:
        """Frame a procedure body at section level; returns whether a statement run is still open."""
        opens_first = not in_run
        for i, stmt in enumerate(inlined.body):
            if isinstance(stmt, ir.CommentStmt):
                self._out.append(stmt.text)
            elif isinstance(stmt, ir.Inlined):
                in_run = self._emit_inlined(stmt, header, in_run)
            elif isinstance(stmt, ir.If) or not in_run:
                if not (opens_first and i == 0 and inlined.opens_with_statement):
                    self._out.append("")
                self._out.append(header)
                in_run = self._emit_framed(stmt)
            else:
                self._emit_stmt(stmt, 1)
        return in_run

    def _emit_framed(self, stmt: ir.Stmt) -> bool:
        """Emit the statement that follows a hook line; returns whether it opened a statement run."""
        if isinstance(stmt, ir.If):
            self._emit_if(stmt, 0)
            return False
        self._emit_stmt(stmt, 1)
        return True

    @staticmethod
    def _indent(text: str, level: int) -> str:
        return f"{' ' * (SystemDefaults.INDENT_SPACES * level)}{text}"

    def _emit_stmt(self, stmt: ir.Stmt, level: int) -> None:
        match stmt:
            case ir.CommentStmt():
                self._out.append(self._indent(stmt.text, level))
            case ir.BreakStmt():
                self._out.append(self._indent("no-op [L]", level))
            case ir.SetOp():
                self._out.append(self._indent(self._render_set_op(stmt), level))
            case ir.SetVar():
                self._out.append(self._indent(stmt.symbol.as_operator(self._render_value(stmt.value)), level))
            case ir.CallOp():
                self._out.append(self._indent(self._render_call(stmt), level))
            case ir.If():
                self._emit_if(stmt, level)
            case ir.Inlined():
                # Inside a block a procedure body is plain statements; only section level frames it.
                for s in stmt.body:
                    self._emit_stmt(s, level)

    def _render_set_op(self, stmt: ir.SetOp) -> str:
        target = stmt.entry.target
        value = self._render_value(stmt.value)

        if stmt.qualifier is None:
            return f"{target} {value}"
        if stmt.variant is ir.OpVariant.ADD:
            return f"{HeaderOperations.ADD_OPERATION} {stmt.qualifier} {value}"
        if isinstance(target, (list, tuple)):
            if stmt.variant is ir.OpVariant.REMOVE:
                return f"{target[0]} {stmt.qualifier}"
            return f"{target[1]} {stmt.qualifier} {value}"
        return f"{target} {stmt.qualifier} {value}"

    def _render_call(self, stmt: ir.CallOp) -> str:
        args = [self._render_value(a) for a in stmt.args]
        line = stmt.entry.target
        if args:
            line = f"{line} {' '.join(args)}"
        if stmt.key == MagicStrings.KEEP_QUERY:
            return f"{line} [I]"
        return line

    def _render_value(self, value: ir.RValue) -> str:
        if isinstance(value, ir.SetLit):
            return f"({value.text})"
        return ir.value_text(value)

    def _emit_if(self, stmt: ir.If, level: int) -> None:
        nested = level > 0
        if nested:
            self._out.append(self._indent("if", level))
            level += 1

        self._emit_cond_chain(stmt.cond, level)
        for s in stmt.body:
            self._emit_stmt(s, level + 1)

        for arm in stmt.elifs:
            self._out.append(self._indent("elif", level))
            self._emit_cond_chain(arm.cond, level + 1)
            for s in arm.body:
                self._emit_stmt(s, level + 2)

        if stmt.has_else:
            # On has_else, not on the body: an `else {}` the sandbox allowed still emits its line.
            self._out.append(self._indent("else", level))
            for s in stmt.else_body:
                self._emit_stmt(s, level + 1)

        if nested:
            self._out.append(self._indent("endif", level - 1))

    def _emit_cond_chain(self, cond: ir.Cond, level: int) -> None:
        self._state = CondState()
        self._queued = None
        self._emit_cond(cond, level, last=True)
        self._flush()

    def _flush(self) -> None:
        if self._queued is None:
            return
        text, state, indent = self._queued
        self._out.append(self._indent(f"{text}{state.render_suffix()}", indent))
        self._queued = None

    def _queue(self, text: str, level: int) -> None:
        if self._queued is not None:
            self._flush()
        self._queued = (text, self._state.copy(), level)
        self._state.reset()

    def _finalize_lhs(self, is_or: bool, level: int) -> None:
        if self._queued is not None:
            text, state, indent = self._queued
            state.and_or = is_or
            if not is_or:
                indent = level
            self._queued = (text, state, indent)
        self._flush()

    def _emit_cond(self, cond: ir.Cond, level: int, *, last: bool) -> None:
        match cond:
            case ir.Or():
                self._emit_cond(cond.left, level, last=False)
                self._finalize_lhs(True, level)
                self._emit_cond(cond.right, level, last=last)
            case ir.And():
                self._emit_cond(cond.left, level, last=False)
                self._finalize_lhs(False, level)
                self._emit_cond(cond.right, level, last=last)
            case ir.Not():
                if isinstance(cond.inner, ir.Group):
                    self._out.append(self._indent("cond %{GROUP}", level))
                    self._emit_cond(cond.inner.inner, level + 1, last=True)
                    self._flush()
                    self._state.last = last
                    self._state.not_ = True
                    self._queue("cond %{GROUP:END}", level)
                else:
                    self._state.not_ = True
                    self._emit_cond(cond.inner, level, last=last)
            case ir.Group():
                self._out.append(self._indent("cond %{GROUP}", level))
                self._emit_cond(cond.inner, level + 1, last=True)
                self._flush()
                self._state.last = last
                self._queue("cond %{GROUP:END}", level)
            case ir.Unresolved():
                pass  # already reported; emitting a guard here would change what the rule matches
            case ir.BoolCond():
                self._state.last = last
                if cond.value:
                    self._queue("cond %{TRUE}", level)
                else:
                    self._queue("cond %{FALSE}", level)
            case ir.FuncCond():
                self._state.last = last
                # Through from_function, not by joining here: emptiness belongs on the arg
                # list, so access("") keeps its colon.
                ref = ir.BuiltinRef.from_function(cond.key, cond.entry, tuple(a.text for a in cond.args))
                self._queue(f"cond {ref.as_value_text()}", level)
            case ir.Truth():
                self._emit_truth(cond, level, last=last)
            case ir.Test():
                self._emit_test(cond, level, last=last)

    def _emit_test(self, cond: ir.Test, level: int, *, last: bool) -> None:
        lhs = cond.ref.as_value_text()
        rhs = self._render_value(cond.rhs)

        match cond.op:
            case ir.CmpOp.EQ | ir.CmpOp.NE:
                text = f"{lhs} ={rhs}"
            case ir.CmpOp.MATCH | ir.CmpOp.NOT_MATCH | ir.CmpOp.IN | ir.CmpOp.NOT_IN:
                text = f"{lhs} {rhs}"
            case _:
                text = f"{lhs} {cond.op.value}{rhs}"

        for mod in cond.modifiers:
            self._state.add_modifier(mod)

        self._state.not_ ^= cond.op in ir.NEGATED_OPS
        self._state.last = last
        self._queue(f"cond {text}", level)

    def _emit_truth(self, cond: ir.Truth, level: int, *, last: bool) -> None:
        symbol = cond.ref.as_value_text()
        if cond.existence:
            text = f'{symbol} =""'
            negate = not self._state.not_
        else:
            text = symbol
            negate = self._state.not_

        self._state.not_ = negate
        self._state.last = last
        self._queue(f"cond {text}", level)
