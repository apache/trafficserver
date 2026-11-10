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
"""Render an AST back to hrw4u source.

Whitespace is not reproduced: the emitter derives its own indentation and never reads the
source's.
"""

from __future__ import annotations

from hrw4u.ast_nodes import *

INDENT = "    "


def unparse(ast: HRW4UAST) -> str:
    return "\n".join(_top_level(item) for item in ast.body) + "\n"


def _top_level(node: TopLevelNode) -> str:
    match node:
        case Comment():
            return node.text
        case UseDirective():
            return f"use {node.spec}"
        case ProcedureDecl():
            params = ", ".join(_proc_param(p) for p in node.params)
            return _braced(f"procedure {node.name}({params})", _body(node.body))
        case VarSection():
            keyword = "SESSION_VARS" if node.scope == "session" else "VARS"
            return _braced(keyword, [_var_item(d) for d in node.items])
        case Section():
            return _braced(node.type, _body(node.body))
    raise ValueError(f"unparse: unhandled top-level node {type(node).__name__}")


def _braced(header: str, lines: list[str]) -> str:
    return "\n".join([f"{header} {{", *(f"{INDENT}{line}" for line in lines), "}"])


def _proc_param(p: ProcParam) -> str:
    return f"${p.name}" if p.default is None else f"${p.name}={_value(p.default)}"


def _var_item(node: VarDecl | Comment) -> str:
    if isinstance(node, Comment):
        return node.text
    slot = "" if node.slot is None else f" @{node.slot}"
    return f"{node.name}: {node.type_name}{slot};"


def _body(body: tuple[BodyNode, ...]) -> list[str]:
    """Lines indented relative to the enclosing block, which adds its own indentation."""
    return [line for item in body for line in _body_item(item)]


def _body_item(node: BodyNode) -> list[str]:
    match node:
        case Comment():
            return [node.text]
        case Break():
            return ["break;"]
        case FunctionCall():
            return [f"{_call(node)};"]
        case Assignment():
            return [f"{node.name} {node.operator} {_value(node.value)};"]
        case IfBlock():
            return _if_block(node)
    raise ValueError(f"unparse: unhandled body node {type(node).__name__}")


def _if_block(node: IfBlock) -> list[str]:
    lines = [f"if {_condition(node.condition)} {{", *_nested(node.body)]
    for arm in node.elif_branches:
        lines += [f"}} elif {_condition(arm.condition)} {{", *_nested(arm.body)]
    if node.has_else:
        lines += ["} else {", *_nested(node.else_body)]
    lines.append("}")
    return lines


def _nested(body: tuple[BodyNode, ...]) -> list[str]:
    return [f"{INDENT}{line}" for line in _body(body)]


def _call(node: FunctionCall) -> str:
    return f"{node.name}({', '.join(_value(a) for a in node.args)})"


def _condition(node: ConditionExpr) -> str:
    match node:
        case Group():
            return f"({_condition(node.inner)})"
        case LogicalOp():
            return f"{_condition(node.left)} {node.operator} {_condition(node.right)}"
        case NotOp():
            return f"!{_condition(node.operand)}"
        case BoolLiteral():
            return "true" if node.value else "false"
        case IdentCondition():
            return node.name
        case FunctionCall():
            return _call(node)
        case Comparison():
            return _comparison(node)
    raise ValueError(f"unparse: unhandled condition node {type(node).__name__}")


def _comparison(node: Comparison) -> str:
    left = node.left.raw if isinstance(node.left, IdentValue) else _call(node.left)
    mods = f" with {', '.join(node.modifiers)}" if node.modifiers else ""
    return f"{left} {node.operator} {_value(node.right)}{mods}"


def _value(v: ValueExpr | RegexValue | SetValue) -> str:
    match v:
        case LiteralStringValue():
            return f'"{v.raw}"'
        case NumberValue() | BoolValue() | IdentValue() | IPValue() | IpRangeValue():
            return v.raw
        case ParamRef():
            return f"${v.raw}"
        case RegexValue():
            return v.raw
        case SetValue():
            return f"[{v.raw}]"
    raise ValueError(f"unparse: unhandled value {type(v).__name__}")
