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
from enum import Enum, auto

__all__ = [
    "AssignOp",
    "CmpOp",
    "BoolOp",
    "VarSectionKind",
    "LiteralStringValue",
    "IdentValue",
    "IPValue",
    "ParamRef",
    "RegexValue",
    "ValueExpr",
    "Node",
    "Target",
    "Assignment",
    "FunctionCall",
    "Break",
    "Comparison",
    "LogicalOp",
    "NotOp",
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


# Enum.__str__ yields "AssignOp.ASSIGN" while the default Enum.__repr__ yields
# "<AssignOp.ASSIGN: 1>"; we alias __repr__ to __str__ on every operator enum
# so that pprint output (used by hrw4u-ast) is concise and readable.


class AssignOp(Enum):
    ASSIGN = auto()
    PLUS_ASSIGN = auto()
    __repr__ = Enum.__str__


class CmpOp(Enum):
    EQ = auto()
    NEQ = auto()
    GT = auto()
    LT = auto()
    MATCH = auto()
    NOT_MATCH = auto()
    IN = auto()
    NOT_IN = auto()
    __repr__ = Enum.__str__


class BoolOp(Enum):
    AND = auto()
    OR = auto()
    __repr__ = Enum.__str__


class VarSectionKind(Enum):
    TXN = auto()
    SESSION = auto()
    __repr__ = Enum.__str__


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


ValueExpr = LiteralStringValue | IdentValue | IPValue | ParamRef | int | bool | tuple[IPValue, ...]


@dataclass(frozen=True, kw_only=True)
class Node:
    line: int


@dataclass(frozen=True)
class Target:
    # Value class, not an AST node — destructured from an Assignment's IDENT
    # lhs, so source position lives on the enclosing Assignment.
    namespace: str | None
    field: str

    @staticmethod
    def from_dotted(name: str) -> Target:
        # TODO: the grammar lexes dotted paths as a single IDENT token;
        # ideally the grammar would split namespace/field so this
        # heuristic isn't needed.
        dot = name.rfind(".")
        if dot == -1:
            return Target(namespace=None, field=name)
        return Target(namespace=name[:dot], field=name[dot + 1:])


@dataclass(frozen=True, kw_only=True)
class Assignment(Node):
    target: Target
    operator: AssignOp
    value: ValueExpr


@dataclass(frozen=True, kw_only=True)
class FunctionCall(Node):
    name: str
    args: tuple[ValueExpr, ...]


@dataclass(frozen=True, kw_only=True)
class Break(Node):
    pass


@dataclass(frozen=True, kw_only=True)
class Comparison(Node):
    left: IdentValue | FunctionCall
    operator: CmpOp
    right: ValueExpr | RegexValue | tuple[ValueExpr, ...]
    modifiers: tuple[str, ...]


@dataclass(frozen=True, kw_only=True)
class LogicalOp(Node):
    operator: BoolOp
    left: ConditionExpr
    right: ConditionExpr


@dataclass(frozen=True, kw_only=True)
class NotOp(Node):
    operand: ConditionExpr


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
    scope: VarSectionKind
    declarations: tuple[VarDecl, ...]


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
ConditionExpr = Comparison | LogicalOp | NotOp | BoolLiteral | IdentCondition | FunctionCall
BodyNode = Assignment | FunctionCall | IfBlock | Break
TopLevelNode = UseDirective | VarSection | ProcedureDecl | Section
