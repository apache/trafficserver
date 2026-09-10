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
from typing import Union

__all__ = [
    "Span",
    "LiteralStringValue",
    "IdentValue",
    "IPValue",
    "ParamRef",
    "RegexValue",
    "SetValue",
    "IpRangeValue",
    "ValueExpr",
    "Node",
    "Assignment",
    "FunctionCall",
    "Break",
    "Comment",
    "Comparison",
    "LogicalOp",
    "NotOp",
    "Group",
    "BoolLiteral",
    "IdentCondition",
    "ElifBranch",
    "IfBlock",
    "Section",
    "ProcParam",
    "VarDecl",
    "VarSection",
    "UseDirective",
    "ProcedureDecl",
    "HRW4UAST",
    "ConditionExpr",
    "BodyNode",
    "TopLevelNode",
]


@dataclass(frozen=True, kw_only=True)
class LiteralStringValue:
    raw: str


@dataclass(frozen=True, kw_only=True)
class IdentValue:
    raw: str


@dataclass(frozen=True, kw_only=True)
class IPValue:
    raw: str


@dataclass(frozen=True, kw_only=True)
class ParamRef:
    raw: str


@dataclass(frozen=True, kw_only=True)
class RegexValue:
    raw: str


@dataclass(frozen=True, kw_only=True)
class SetValue:
    """An `in [...]` operand. Emitted as `(raw)`, so the brackets are stripped but quoting is not."""
    raw: str


@dataclass(frozen=True, kw_only=True)
class IpRangeValue:
    """An `in {...}` operand. Emitted verbatim, braces included."""
    raw: str


ValueExpr = Union[LiteralStringValue, IdentValue, IPValue, ParamRef, int, bool, IpRangeValue]


@dataclass(frozen=True, slots=True)
class Span:
    """Start position of a node. `line` is 1-based and `column` 0-based, matching ANTLR tokens."""
    file: str
    line: int
    column: int


@dataclass(frozen=True, kw_only=True)
class Node:
    span: Span


@dataclass(frozen=True, kw_only=True)
class Assignment(Node):
    name: str
    operator: str  # "=" or "+="
    value: ValueExpr


@dataclass(frozen=True, kw_only=True)
class FunctionCall(Node):
    name: str
    args: tuple[ValueExpr, ...]


@dataclass(frozen=True, kw_only=True)
class Break(Node):
    pass


@dataclass(frozen=True, kw_only=True)
class Comment(Node):
    text: str


@dataclass(frozen=True, kw_only=True)
class Comparison(Node):
    left: IdentValue | FunctionCall
    operator: str  # "==", "!=", ">", "<", "~", "!~", "in", "!in"
    right: ValueExpr | RegexValue | SetValue
    modifiers: tuple[str, ...]


@dataclass(frozen=True, kw_only=True)
class LogicalOp(Node):
    operator: str  # "&&" or "||"
    left: ConditionExpr
    right: ConditionExpr


@dataclass(frozen=True, kw_only=True)
class NotOp(Node):
    operand: ConditionExpr


@dataclass(frozen=True, kw_only=True)
class Group(Node):
    inner: ConditionExpr


@dataclass(frozen=True, kw_only=True)
class BoolLiteral(Node):
    value: bool


@dataclass(frozen=True, kw_only=True)
class IdentCondition(Node):
    name: str


@dataclass(frozen=True, kw_only=True)
class ElifBranch(Node):
    condition: ConditionExpr
    body: tuple[BodyNode, ...]


@dataclass(frozen=True, kw_only=True)
class IfBlock(Node):
    condition: ConditionExpr
    body: tuple[BodyNode, ...]
    elif_branches: tuple[ElifBranch, ...]
    else_body: tuple[BodyNode, ...]


@dataclass(frozen=True, kw_only=True)
class Section(Node):
    type: str
    body: tuple[BodyNode, ...]


@dataclass(frozen=True, kw_only=True)
class ProcParam(Node):
    name: str
    default: ValueExpr | None


@dataclass(frozen=True, kw_only=True)
class VarDecl(Node):
    name: str
    type_name: str
    slot: int | None


@dataclass(frozen=True, kw_only=True)
class VarSection(Node):
    scope: str
    items: tuple[VarDecl | Comment, ...]


@dataclass(frozen=True, kw_only=True)
class UseDirective(Node):
    spec: str


@dataclass(frozen=True, kw_only=True)
class ProcedureDecl(Node):
    name: str
    params: tuple[ProcParam, ...]
    body: tuple[BodyNode, ...]


@dataclass(frozen=True, kw_only=True)
class HRW4UAST:
    body: tuple[TopLevelNode, ...]


# Type aliases: must follow all class definitions (evaluated at runtime).
ConditionExpr = Union[Comparison, LogicalOp, NotOp, Group, BoolLiteral, IdentCondition, FunctionCall]
BodyNode = Union[Assignment, FunctionCall, IfBlock, Break, Comment]
TopLevelNode = Union[UseDirective, VarSection, ProcedureDecl, Section, Comment]
