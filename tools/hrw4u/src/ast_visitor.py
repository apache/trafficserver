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
from hrw4u.common import SystemDefaults


class ASTVisitor(hrw4uVisitor):
    """
    ANTLR visitor that walks an HRW4U parse tree and produces an AST for HRW4U.

    Requires a tree that parsed without errors. ANTLR's error recovery can leave a mandatory
    child unset, which surfaces here as an AttributeError rather than a diagnostic.
    """

    def __init__(self, filename: str = SystemDefaults.DEFAULT_FILENAME) -> None:
        super().__init__()
        self.filename = filename

    def _span(self, ctx) -> Span:
        return Span(file=self.filename, line=ctx.start.line, column=ctx.start.column)

    def _unhandled(self, what: str, ctx) -> ValueError:
        span = self._span(ctx)
        return ValueError(f"Unhandled {what} at {span.file}:{span.line}:{span.column}")

    def _visit_comment(self, ctx) -> Comment:
        return Comment(text=ctx.COMMENT().getText(), span=self._span(ctx))

    # Only visitProgram is overridden from the ANTLR visitor interface;
    # all other traversal uses private _visit_* helpers so that each
    # method has an explicit return type and full control over how
    # child results are assembled into parent AST nodes.

    def visitProgram(self, ctx) -> HRW4UAST:
        items = []
        for item in ctx.programItem():
            if item.useDirective() is not None:
                items.append(self._visit_use_directive(item.useDirective()))
            elif item.procedureDecl() is not None:
                items.append(self._visit_procedure_decl(item.procedureDecl()))
            elif item.section() is not None:
                items.append(self._visit_section(item.section()))
            elif item.commentLine() is not None:
                items.append(self._visit_comment(item.commentLine()))
            else:
                raise self._unhandled("programItem alternative", item)
        return HRW4UAST(body=tuple(items))

    def _visit_use_directive(self, ctx) -> UseDirective:
        return UseDirective(spec=ctx.QUALIFIED_IDENT().getText(), span=self._span(ctx))

    def _visit_procedure_decl(self, ctx) -> ProcedureDecl:
        name = ctx.QUALIFIED_IDENT().getText()
        params = ()
        if ctx.paramList():
            params = tuple(self._visit_proc_param(p) for p in ctx.paramList().param())
        body = tuple(self._visit_body(ctx.block().blockItem()))
        return ProcedureDecl(name=name, params=params, body=body, span=self._span(ctx))

    def _visit_proc_param(self, ctx) -> ProcParam:
        name = ctx.IDENT().getText()
        default = self._extract_value(ctx.value()) if ctx.value() else None
        return ProcParam(name=name, default=default, span=self._span(ctx))

    def _visit_section(self, ctx) -> VarSection | Section:
        if ctx.varSection() is not None:
            return self._visit_var_section(ctx.varSection(), "txn")
        if ctx.sessionVarSection() is not None:
            return self._visit_var_section(ctx.sessionVarSection(), "session")
        name = ctx.name.text
        body = self._visit_body(ctx.sectionBody())
        return Section(type=name, body=tuple(body), span=self._span(ctx))

    def _visit_var_section(self, ctx, scope) -> VarSection:
        items = []
        for var_item in ctx.variables().variablesItem():
            if var_item.variableDecl() is not None:
                items.append(self._visit_var_decl(var_item.variableDecl()))
            elif var_item.commentLine() is not None:
                items.append(self._visit_comment(var_item.commentLine()))
            else:
                raise self._unhandled("variablesItem alternative", var_item)
        return VarSection(scope=scope, items=tuple(items), span=self._span(ctx))

    def _visit_var_decl(self, ctx) -> VarDecl:
        return VarDecl(
            name=ctx.name.text, type_name=ctx.typeName.text, slot=int(ctx.slot.text) if ctx.slot else None, span=self._span(ctx))

    def _visit_body(self, items) -> list[BodyNode]:
        """Shared helper for sectionBody and blockItem lists."""
        result = []
        for item in items:
            if item.statement() is not None:
                result.append(self._visit_statement(item.statement()))
            elif item.conditional() is not None:
                result.append(self._visit_conditional(item.conditional()))
            elif item.commentLine() is not None:
                result.append(self._visit_comment(item.commentLine()))
            else:
                raise self._unhandled("body item alternative", item)
        return result

    def _visit_statement(self, ctx) -> BodyNode:
        if ctx.BREAK():
            return Break(span=self._span(ctx))
        if ctx.functionCall():
            return self._visit_function_call(ctx.functionCall())
        if ctx.EQUAL():
            return Assignment(name=ctx.lhs.text, operator="=", value=self._extract_value(ctx.value()), span=self._span(ctx))
        if ctx.PLUSEQUAL():
            return Assignment(name=ctx.lhs.text, operator="+=", value=self._extract_value(ctx.value()), span=self._span(ctx))
        if ctx.op:
            return FunctionCall(name=ctx.op.text, args=(), span=self._span(ctx))
        raise self._unhandled("statement alternative", ctx)

    def _visit_function_call(self, ctx) -> FunctionCall:
        name = ctx.funcName.text
        args = ()
        if ctx.argumentList():
            args = tuple(self._extract_value(v) for v in ctx.argumentList().value())
        return FunctionCall(name=name, args=args, span=self._span(ctx))

    def _extract_value(self, ctx) -> ValueExpr:
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
            return IpRangeValue(raw=ctx.iprange().getText())
        if ctx.paramRef():
            return ParamRef(raw=ctx.paramRef().IDENT().getText())
        raise self._unhandled("value alternative", ctx)

    def _visit_conditional(self, ctx) -> IfBlock:
        if_stmt = ctx.ifStatement()
        condition = self._visit_condition(if_stmt.condition())
        body = tuple(self._visit_body(if_stmt.block().blockItem()))

        elif_branches = []
        for elif_ctx in ctx.elifClause():
            elif_cond = self._visit_condition(elif_ctx.condition())
            elif_body = tuple(self._visit_body(elif_ctx.block().blockItem()))
            elif_branches.append(ElifBranch(condition=elif_cond, body=elif_body, span=self._span(elif_ctx)))

        else_body = ()
        if ctx.elseClause():
            else_body = tuple(self._visit_body(ctx.elseClause().block().blockItem()))

        return IfBlock(
            condition=condition, body=body, elif_branches=tuple(elif_branches), else_body=else_body, span=self._span(ctx))

    def _visit_condition(self, ctx) -> ConditionExpr:
        return self._visit_expression(ctx.expression())

    def _visit_expression(self, ctx) -> ConditionExpr:
        if ctx.OR():
            left = self._visit_expression(ctx.expression())
            right = self._visit_term(ctx.term())
            return LogicalOp(operator="||", left=left, right=right, span=self._span(ctx))
        return self._visit_term(ctx.term())

    def _visit_term(self, ctx) -> ConditionExpr:
        if ctx.AND():
            left = self._visit_term(ctx.term())
            right = self._visit_factor(ctx.factor())
            return LogicalOp(operator="&&", left=left, right=right, span=self._span(ctx))
        return self._visit_factor(ctx.factor())

    def _visit_factor(self, ctx) -> ConditionExpr:
        if ctx.getChildCount() == 2 and ctx.getChild(0).getText() == "!":
            return NotOp(operand=self._visit_factor(ctx.factor()), span=self._span(ctx))
        if ctx.LPAREN():
            return Group(inner=self._visit_expression(ctx.expression()), span=self._span(ctx))
        if ctx.functionCall():
            return self._visit_function_call(ctx.functionCall())
        if ctx.comparison():
            return self._visit_comparison(ctx.comparison())
        if ctx.ident is not None:
            return IdentCondition(name=ctx.ident.text, span=self._span(ctx))
        if ctx.TRUE():
            return BoolLiteral(value=True, span=self._span(ctx))
        if ctx.FALSE():
            return BoolLiteral(value=False, span=self._span(ctx))
        raise self._unhandled("factor alternative", ctx)

    def _visit_comparison(self, ctx) -> Comparison:
        comp = ctx.comparable()
        if comp.ident is not None:
            left = IdentValue(raw=comp.ident.text)
        else:
            left = self._visit_function_call(comp.functionCall())

        operator = self._detect_comparison_operator(ctx)
        right = self._extract_comparison_rhs(ctx, operator)
        modifiers = self._extract_modifiers(ctx)

        return Comparison(left=left, operator=operator, right=right, modifiers=modifiers, span=self._span(ctx))

    def _detect_comparison_operator(self, ctx) -> str:
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
        raise self._unhandled("comparison operator", ctx)

    def _extract_comparison_rhs(self, ctx, operator) -> ValueExpr | RegexValue | SetValue:
        if operator in ("~", "!~"):
            return RegexValue(raw=ctx.regex().getText()[1:-1])
        if operator in ("in", "!in"):
            if ctx.set_():
                return SetValue(raw=ctx.set_().getText()[1:-1])
            if ctx.iprange():
                return IpRangeValue(raw=ctx.iprange().getText())
        if ctx.value():
            return self._extract_value(ctx.value())
        raise self._unhandled("comparison RHS", ctx)

    def _extract_modifiers(self, ctx) -> tuple[str, ...]:
        if ctx.modifier():
            return tuple(tok.text for tok in ctx.modifier().modifierList().mods)
        return ()
