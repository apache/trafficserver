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

from dataclasses import dataclass
from enum import StrEnum
from typing import Union

from hrw4u.ast_nodes import Span
from hrw4u.common import apply_percent_mods
from hrw4u.states import SectionType
from hrw4u.types import MapParams, Symbol


class CmpOp(StrEnum):
    EQ = "=="
    NE = "!="
    GT = ">"
    LT = "<"
    MATCH = "~"
    NOT_MATCH = "!~"
    IN = "in"
    NOT_IN = "!in"


NEGATED_OPS = frozenset({CmpOp.NE, CmpOp.NOT_MATCH, CmpOp.NOT_IN})


class OpVariant(StrEnum):
    SET = "SET"
    REMOVE = "REMOVE"
    ADD = "ADD"


@dataclass(frozen=True, slots=True)
class BuiltinRef:
    """A read of a tables.py entry. `arg` is the normalized suffix of a prefix key, or the
    joined argument list of a function call."""
    key: str
    entry: MapParams
    arg: str | None = None
    wrap: bool = False  # CONDITION_MAP targets are pre-wrapped ("%{METHOD}"); FUNCTION_MAP targets are bare ("CIDR").

    @classmethod
    def from_function(cls, key: str, entry: MapParams, args: tuple[str, ...] | list[str]) -> BuiltinRef:
        """A FUNCTION_MAP call. Keeps the %{} wrapping of the bare target and the `,` joiner in
        one place. Empties on the arg list, not on the joined text, so `[""]` stays an argument."""
        arg = None
        if args:
            arg = ",".join(args)
        return cls(key=key, entry=entry, arg=arg, wrap=True)

    def as_value_text(self) -> str:
        # header_rewrite's %{} value syntax, shared by hrw4u source. Sema needs it because
        # argument validators run on post-substitution text.
        if self.arg is not None:
            return f"%{{{self.entry.target}:{self.arg}}}"
        if self.wrap:
            return f"%{{{self.entry.target}}}"
        return str(self.entry.target)


@dataclass(frozen=True, slots=True)
class VarRef:
    symbol: Symbol

    def as_value_text(self) -> str:
        return self.symbol.as_cond()


Ref = Union[BuiltinRef, VarRef]


@dataclass(frozen=True, slots=True)
class ModifiedRef:
    """An interpolated reference carrying value modifiers, e.g. `{inbound.url.path with NORM}`."""
    ref: Ref
    mods: tuple[str, ...]

    def as_value_text(self) -> str:
        return apply_percent_mods(self.ref.as_value_text(), list(self.mods))


@dataclass(frozen=True, slots=True)
class RawArg:
    """Verbatim source text for a non-interpolated value, post-normalization."""
    text: str


@dataclass(frozen=True, slots=True)
class StrTemplate:
    """A string literal split into literal chunks and resolved references."""
    parts: tuple[Union[str, BuiltinRef, VarRef, ModifiedRef], ...]


@dataclass(frozen=True, slots=True)
class RegexLit:
    text: str  # the literal as written, delimiters included, so a validator sees what the source said


@dataclass(frozen=True, slots=True)
class SetLit:
    text: str  # inner text of [...], brackets stripped; the emitter re-wraps it as (...)


@dataclass(frozen=True, slots=True)
class IpRangeLit:
    text: str  # braces included, emitted verbatim as {...}


RValue = Union[RawArg, StrTemplate, BuiltinRef, VarRef, RegexLit, SetLit, IpRangeLit]


def _part_text(part: str | BuiltinRef | VarRef | ModifiedRef) -> str:
    if isinstance(part, str):
        return part
    return part.as_value_text()


def value_text(value: RValue) -> str:
    """A value as hrw4u source spells it: what validators see. The .conf emitter differs only on SetLit."""
    match value:
        case StrTemplate():
            body = ''.join(_part_text(p) for p in value.parts)
            return f'"{body}"'
        case RawArg() | RegexLit() | SetLit() | IpRangeLit():
            return value.text
        case _:
            return value.as_value_text()


@dataclass(frozen=True, slots=True)
class And:
    left: Cond
    right: Cond
    span: Span


@dataclass(frozen=True, slots=True)
class Or:
    left: Cond
    right: Cond
    span: Span


@dataclass(frozen=True, slots=True)
class Not:
    inner: Cond
    span: Span


@dataclass(frozen=True, slots=True)
class Group:
    """Parentheses kept so the emitter can reproduce them. Shares its name with
    ast_nodes.Group: a module needing both must import at least one qualified, since a flat
    import of both would shadow silently."""
    inner: Cond
    span: Span


@dataclass(frozen=True, slots=True)
class Test:
    __test__ = False  # not a pytest test class; pytest collects by name
    ref: Ref
    op: CmpOp
    rhs: RValue
    modifiers: tuple[str, ...]
    span: Span


@dataclass(frozen=True, slots=True)
class Truth:
    """A bare reference in boolean position. `existence` is the old default_expr flag.
    No modifiers: the grammar attaches `modifier?` to `comparison`, not to `factor`."""
    ref: Ref
    existence: bool
    span: Span


@dataclass(frozen=True, slots=True)
class BoolCond:
    value: bool
    span: Span


@dataclass(frozen=True, slots=True)
class Unresolved:
    """A condition sema could not resolve; the failure is already reported, and a run that
    reported an error writes no configuration, so the emitter renders nothing."""
    span: Span


@dataclass(frozen=True, slots=True)
class FuncCond:
    key: str
    entry: MapParams
    args: tuple[RawArg, ...]
    span: Span


Cond = Union[And, Or, Not, Group, Test, Truth, BoolCond, FuncCond, Unresolved]


@dataclass(frozen=True, slots=True)
class SetOp:
    key: str
    entry: MapParams
    qualifier: str | None
    value: RValue
    variant: OpVariant
    span: Span


@dataclass(frozen=True, slots=True)
class SetVar:
    symbol: Symbol
    value: RValue
    span: Span


@dataclass(frozen=True, slots=True)
class CallOp:
    key: str
    entry: MapParams
    args: tuple[RValue, ...]
    span: Span


@dataclass(frozen=True, slots=True)
class BreakStmt:
    span: Span


@dataclass(frozen=True, slots=True)
class CommentStmt:
    text: str
    span: Span


@dataclass(frozen=True, slots=True)
class ElifArm:
    cond: Cond
    body: tuple[Stmt, ...]
    span: Span


@dataclass(frozen=True, slots=True)
class If:
    cond: Cond
    body: tuple[Stmt, ...]
    elifs: tuple[ElifArm, ...]
    else_body: tuple[Stmt, ...]
    span: Span
    has_else: bool = False  # an `else {}` the sandbox allowed still emits its `else` line


@dataclass(frozen=True, slots=True)
class Inlined:
    """A procedure body spliced in at a call. Kept as a node rather than flattened because at
    section level a procedure body is framed by its own rules: the hook line is not
    re-emitted for the call itself, and a blank line precedes every hook the body opens
    except one opened by the body's first source item. `opens_with_statement` says whether
    `body[0]` is that item -- not so when it was a comment, even one dropped from the output."""
    body: tuple[Stmt, ...]
    opens_with_statement: bool
    span: Span


Stmt = Union[SetOp, SetVar, CallOp, BreakStmt, CommentStmt, If, Inlined]


@dataclass(frozen=True, slots=True)
class ResolvedSection:
    section: SectionType
    body: tuple[Stmt, ...]
    span: Span

    @property
    def hook(self) -> str:
        # Derived, not stored: a stored copy could disagree with `section`.
        return self.section.hook_name


@dataclass(frozen=True, slots=True)
class VarsBlock:
    """A VARS/SESSION_VARS section. Declarations are consumed by sema; only its
    comments reach the output, and it counts as a program item for the blank-line
    separator rule."""
    comments: tuple[CommentStmt, ...]
    span: Span


@dataclass(frozen=True, slots=True)
class Program:
    items: tuple[Union[ResolvedSection, VarsBlock, CommentStmt], ...]
