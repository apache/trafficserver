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

from hrw4u.hrw4uVisitor import hrw4uVisitor
from hrw4u.ast_nodes import *


class ASTVisitor(hrw4uVisitor):
    """ANTLR visitor that walks an HRW4U parse tree and produces an AST for HRW4U."""

    # Only visitProgram is overridden from the ANTLR visitor interface;
    # all other traversal uses private _visit_* helpers so that each
    # method has an explicit return type and full control over how
    # child results are assembled into parent AST nodes.

    def visitProgram(self, ctx):
        items = []
        for item in ctx.programItem():
            if item.useDirective() is not None:
                items.append(self._visit_use_directive(item.useDirective()))
            elif item.procedureDecl() is not None:
                items.append(self._visit_procedure_decl(item.procedureDecl()))
            elif item.section() is not None:
                items.append(self._visit_section(item.section()))
            elif item.commentLine() is not None:
                pass
            else:
                raise ValueError(f"Unhandled programItem alternative at line {item.start.line}")
        return HRW4UAST(body=tuple(items))

    def _visit_use_directive(self, ctx):
        return UseDirective(
            spec=ctx.QUALIFIED_IDENT().getText(),
            line=ctx.start.line,
        )

    def _visit_procedure_decl(self, ctx):
        name = ctx.QUALIFIED_IDENT().getText()
        params = ()
        if ctx.paramList():
            params = tuple(self._visit_proc_param(p) for p in ctx.paramList().param())
        body = tuple(self._visit_body(ctx.block().blockItem()))
        return ProcedureDecl(name=name, params=params, body=body, line=ctx.start.line)

    def _visit_proc_param(self, ctx):
        name = ctx.IDENT().getText()
        default = self._extract_value(ctx.value()) if ctx.value() else None
        return ProcParam(name=name, default=default, line=ctx.start.line)

    def _visit_section(self, ctx):
        if ctx.varSection() is not None:
            return self._visit_var_section(ctx.varSection(), "txn")
        if ctx.sessionVarSection() is not None:
            return self._visit_var_section(ctx.sessionVarSection(), "session")
        name = ctx.name.text
        body = self._visit_body(ctx.sectionBody())
        return Section(type=name, body=tuple(body), line=ctx.start.line)

    def _visit_var_section(self, ctx, scope):
        decls = []
        for var_item in ctx.variables().variablesItem():
            if var_item.variableDecl() is not None:
                decls.append(self._visit_var_decl(var_item.variableDecl()))
            elif var_item.commentLine() is not None:
                pass
            else:
                raise ValueError(f"Unhandled variablesItem alternative at line {var_item.start.line}")
        return VarSection(scope=scope, declarations=tuple(decls), line=ctx.start.line)

    def _visit_var_decl(self, ctx):
        return VarDecl(
            name=ctx.name.text,
            type_name=ctx.typeName.text,
            slot=int(ctx.slot.text) if ctx.slot else None,
            line=ctx.start.line,
        )

    def _visit_body(self, items):
        """Shared helper for sectionBody and blockItem lists."""
        result = []
        for item in items:
            if item.statement() is not None:
                result.append(self._visit_statement(item.statement()))
            elif item.conditional() is not None:
                result.append(self._visit_conditional(item.conditional()))
            elif item.commentLine() is not None:
                pass
            else:
                raise ValueError(f"Unhandled body item alternative at line {item.start.line}")
        return result

    def _visit_statement(self, ctx):
        line = ctx.start.line
        if ctx.BREAK():
            return Break(line=line)
        if ctx.functionCall():
            return self._visit_function_call(ctx.functionCall())
        if ctx.EQUAL():
            target = Target.from_dotted(ctx.lhs.text)
            value = self._extract_value(ctx.value())
            return Assignment(target=target, operator="=", value=value, line=line)
        if ctx.PLUSEQUAL():
            target = Target.from_dotted(ctx.lhs.text)
            value = self._extract_value(ctx.value())
            return Assignment(target=target, operator="+=", value=value, line=line)
        if ctx.op:
            return FunctionCall(name=ctx.op.text, args=(), line=line)
        raise ValueError(f"Unhandled statement alternative at line {line}")

    def _visit_function_call(self, ctx):
        name = ctx.funcName.text
        args = ()
        if ctx.argumentList():
            args = tuple(self._extract_value(v) for v in ctx.argumentList().value())
        return FunctionCall(name=name, args=args, line=ctx.start.line)

    def _extract_value(self, ctx):
        if ctx.number is not None:
            return int(ctx.number.text)
        if ctx.str_ is not None:
            return LiteralStringValue(raw=ctx.str_.text[1:-1])
        if ctx.TRUE():
            return True
        if ctx.FALSE():
            return False
        if ctx.ident is not None:
            return IdentValue(raw=ctx.ident.text)
        if ctx.ip():
            return IPValue(raw=ctx.ip().getText())
        if ctx.iprange():
            return tuple(IPValue(raw=ip.getText()) for ip in ctx.iprange().ip())
        if ctx.paramRef():
            return ParamRef(raw=ctx.paramRef().IDENT().getText())
        raise ValueError(f"Unhandled value alternative at line {ctx.start.line}")

    def _visit_conditional(self, ctx):
        if_stmt = ctx.ifStatement()
        condition = self._visit_condition(if_stmt.condition())
        block = if_stmt.block()
        body = tuple(self._visit_body(block.blockItem())) if block else ()

        elif_branches = []
        for elif_ctx in ctx.elifClause():
            elif_cond = self._visit_condition(elif_ctx.condition())
            elif_block = elif_ctx.block()
            elif_body = tuple(self._visit_body(elif_block.blockItem())) if elif_block else ()
            elif_branches.append(ElifBranch(
                condition=elif_cond,
                body=elif_body,
                line=elif_ctx.start.line,
            ))

        else_body = ()
        if ctx.elseClause():
            else_block = ctx.elseClause().block()
            if else_block:
                else_body = tuple(self._visit_body(else_block.blockItem()))

        return IfBlock(
            condition=condition,
            body=body,
            elif_branches=tuple(elif_branches),
            else_body=else_body,
            line=ctx.start.line,
        )

    def _visit_condition(self, ctx):
        return self._visit_expression(ctx.expression())

    def _visit_expression(self, ctx):
        if ctx.OR():
            left = self._visit_expression(ctx.expression())
            right = self._visit_term(ctx.term())
            return LogicalOp(
                operator="||",
                left=left,
                right=right,
                line=ctx.start.line,
            )
        return self._visit_term(ctx.term())

    def _visit_term(self, ctx):
        if ctx.AND():
            left = self._visit_term(ctx.term())
            right = self._visit_factor(ctx.factor())
            return LogicalOp(
                operator="&&",
                left=left,
                right=right,
                line=ctx.start.line,
            )
        return self._visit_factor(ctx.factor())

    def _visit_factor(self, ctx):
        if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
            return NotOp(
                operand=self._visit_factor(ctx.factor()),
                line=ctx.start.line,
            )
        if ctx.LPAREN():
            return self._visit_expression(ctx.expression())
        if ctx.functionCall():
            return self._visit_function_call(ctx.functionCall())
        if ctx.comparison():
            return self._visit_comparison(ctx.comparison())
        if ctx.ident is not None:
            return IdentCondition(name=ctx.ident.text, line=ctx.start.line)
        if ctx.TRUE():
            return BoolLiteral(value=True, line=ctx.start.line)
        if ctx.FALSE():
            return BoolLiteral(value=False, line=ctx.start.line)
        raise ValueError(f"Unhandled factor alternative at line {ctx.start.line}")

    def _visit_comparison(self, ctx):
        line = ctx.start.line
        comp = ctx.comparable()
        if comp.ident is not None:
            left = IdentValue(raw=comp.ident.text)
        else:
            left = self._visit_function_call(comp.functionCall())

        operator = self._detect_comparison_operator(ctx)
        right = self._extract_comparison_rhs(ctx, operator)
        modifiers = self._extract_modifiers(ctx)

        return Comparison(
            left=left,
            operator=operator,
            right=right,
            modifiers=modifiers,
            line=line,
        )

    def _detect_comparison_operator(self, ctx):
        if ctx.EQUALS():
            return "=="
        if ctx.NEQ():
            return "!="
        if ctx.GT():
            return ">"
        if ctx.LT():
            return "<"
        if ctx.TILDE():
            return "~"
        if ctx.NOT_TILDE():
            return "!~"
        if ctx.IN():
            for child in ctx.children:
                if hasattr(child, "getText") and child.getText() == "!":
                    return "!in"
            return "in"
        raise ValueError(f"Unhandled comparison operator at line {ctx.start.line}")

    def _extract_comparison_rhs(self, ctx, operator):
        if operator in ("~", "!~"):
            return RegexValue(raw=ctx.regex().getText()[1:-1])
        if operator in ("in", "!in"):
            if ctx.set_():
                return tuple(self._extract_value(v) for v in ctx.set_().value())
            if ctx.iprange():
                return tuple(IPValue(raw=ip.getText()) for ip in ctx.iprange().ip())
        if ctx.value():
            return self._extract_value(ctx.value())
        raise ValueError(f"Unhandled comparison RHS at line {ctx.start.line}")

    def _extract_modifiers(self, ctx):
        if ctx.modifier():
            return tuple(tok.text for tok in ctx.modifier().modifierList().mods)
        return ()
