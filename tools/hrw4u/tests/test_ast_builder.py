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

from hrw4u import ast_nodes as nodes
from utils import parse_input_text
from hrw4u.ast_builder import ASTBuilder


def _build(source: str) -> nodes.HRW4UAST:
    _, tree = parse_input_text(source)
    return ASTBuilder().visit(tree)


class TestAssignments:

    def test_simple_assignment(self):
        ast = _build('REMAP {\n    inbound.req.X-Foo = "test";\n}')
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.target == nodes.Target.from_dotted("inbound.req.X-Foo")
        assert a.operator == nodes.AssignOp.ASSIGN
        assert a.value == nodes.LiteralStringValue(raw="test")

    def test_bool_value(self):
        ast = _build('SEND_RESPONSE {\n    http.cntl.TXN_DEBUG = true;\n}')
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value is True

    def test_false_value(self):
        ast = _build('SEND_RESPONSE {\n    http.cntl.TXN_DEBUG = false;\n}')
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value is False

    def test_empty_string_value(self):
        ast = _build('REMAP {\n    inbound.req.X-Foo = "";\n}')
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value == nodes.LiteralStringValue(raw="")

    def test_int_value(self):
        ast = _build('REMAP {\n    http.cntl.INTERCEPT_RETRY = 1;\n}')
        a = ast.body[0].body[0]
        assert a.value == 1

    def test_plus_equals(self):
        ast = _build('REMAP {\n    inbound.req.X-Foo += "extra";\n}')
        a = ast.body[0].body[0]
        assert a.operator == nodes.AssignOp.PLUS_ASSIGN

    def test_ip_value(self):
        ast = _build('REMAP {\n    inbound.req.X-IP = 10.0.0.1;\n}')
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value == nodes.IPValue(raw="10.0.0.1")

    def test_param_ref_value(self):
        src = 'procedure local::stamp($tag) {\n    inbound.req.X-Stamp = $tag;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        a = ast.body[0].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value == nodes.ParamRef(raw="tag")

    def test_ident_value(self):
        src = 'VARS {\n    flag: bool;\n}\nREMAP {\n    inbound.req.X-Flag = flag;\n}'
        ast = _build(src)
        a = ast.body[1].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.value == nodes.IdentValue(raw="flag")


class TestTarget:

    def test_from_dotted(self):
        t = nodes.Target.from_dotted("inbound.req.X-Foo")
        assert t.namespace == "inbound.req"
        assert t.field == "X-Foo"

    def test_from_dotted_no_namespace(self):
        t = nodes.Target.from_dotted("flag")
        assert t.namespace is None
        assert t.field == "flag"


class TestFunctionCalls:

    def test_no_args(self):
        ast = _build('REMAP {\n    set-debug();\n}')
        fc = ast.body[0].body[0]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.name == "set-debug"
        assert fc.args == ()

    def test_with_args(self):
        ast = _build('REMAP {\n    set-header("X-Foo", "bar");\n}')
        fc = ast.body[0].body[0]
        assert fc.name == "set-header"
        assert fc.args == (nodes.LiteralStringValue(raw="X-Foo"), nodes.LiteralStringValue(raw="bar"))

    def test_qualified_name(self):
        src = 'use test::helper\nREMAP {\n    test::helper("tag");\n}'
        ast = _build(src)
        fc = ast.body[1].body[0]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.name == "test::helper"
        assert fc.args == (nodes.LiteralStringValue(raw="tag"),)

    def test_param_ref_arg(self):
        src = 'procedure local::stamp($tag) {\n    set-header("X-Stamp", $tag);\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        fc = ast.body[0].body[0]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.name == "set-header"
        assert fc.args == (nodes.LiteralStringValue(raw="X-Stamp"), nodes.ParamRef(raw="tag"))

    def test_standalone_operator(self):
        ast = _build('REMAP {\n    skip-remap;\n}')
        fc = ast.body[0].body[0]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.name == "skip-remap"
        assert fc.args == ()

    def test_break(self):
        ast = _build('REMAP {\n    if true {\n        break;\n    }\n}')
        body = ast.body[0].body[0].body
        assert isinstance(body[0], nodes.Break)


class TestSections:

    def test_comments_in_section_body_skipped(self):
        src = 'REMAP {\n    # a comment\n    set-debug();\n    # another comment\n}'
        ast = _build(src)
        assert len(ast.body[0].body) == 1

    def test_comments_in_block_skipped(self):
        src = 'REMAP {\n    if true {\n        # comment\n        set-debug();\n    }\n}'
        ast = _build(src)
        assert len(ast.body[0].body[0].body) == 1

    def test_section_type(self):
        ast = _build('REMAP {\n    set-debug();\n}')
        s = ast.body[0]
        assert isinstance(s, nodes.Section)
        assert s.type == "REMAP"

    def test_multiple_sections(self):
        src = 'REMAP {\n    set-debug();\n}\nSEND_RESPONSE {\n    set-debug();\n}'
        ast = _build(src)
        sections = [i for i in ast.body if isinstance(i, nodes.Section)]
        assert len(sections) == 2
        assert sections[0].type == "REMAP"
        assert sections[1].type == "SEND_RESPONSE"

    def test_use_directive(self):
        src = 'use test::add-debug-header\nREMAP {\n    test::add-debug-header("tag");\n}'
        ast = _build(src)
        assert len(ast.body) == 2
        u = ast.body[0]
        assert isinstance(u, nodes.UseDirective)
        assert u.spec == "test::add-debug-header"

    def test_multiple_use_directives(self):
        src = ('use test::add-debug-header\n'
               'use test::stamp-request\n'
               'REMAP {\n    test::add-debug-header("tag");\n}')
        ast = _build(src)
        directives = [i for i in ast.body if isinstance(i, nodes.UseDirective)]
        assert len(directives) == 2
        assert directives[0].spec == "test::add-debug-header"
        assert directives[1].spec == "test::stamp-request"

    def test_top_level_comments_skipped(self):
        src = ('# leading comment\n'
               'use test::helper\n'
               '# between use and section\n'
               'REMAP {\n    set-debug();\n}\n'
               '# trailing comment\n')
        ast = _build(src)
        assert len(ast.body) == 2
        assert isinstance(ast.body[0], nodes.UseDirective)
        assert isinstance(ast.body[1], nodes.Section)

    def test_item_ordering(self):
        src = 'VARS {\n    x: bool;\n}\nREMAP {\n    set-debug();\n}\nSEND_RESPONSE {\n    set-debug();\n}'
        ast = _build(src)
        assert len(ast.body) == 3
        assert isinstance(ast.body[0], nodes.VarSection)
        assert isinstance(ast.body[1], nodes.Section)
        assert isinstance(ast.body[2], nodes.Section)


class TestVarSections:

    def test_comments_in_var_section_skipped(self):
        src = 'VARS {\n    # comment\n    x: bool;\n    # another\n    y: int;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        vs = ast.body[0]
        assert isinstance(vs, nodes.VarSection)
        assert len(vs.declarations) == 2

    def test_txn_scope(self):
        src = 'VARS {\n    flag: bool;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        vs = ast.body[0]
        assert isinstance(vs, nodes.VarSection)
        assert vs.scope == nodes.VarSectionKind.TXN
        assert len(vs.declarations) == 1
        assert vs.declarations[0].name == "flag"
        assert vs.declarations[0].type_name == "bool"
        assert vs.declarations[0].slot is None

    def test_session_scope(self):
        src = 'SESSION_VARS {\n    counter: int;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        vs = ast.body[0]
        assert isinstance(vs, nodes.VarSection)
        assert vs.scope == nodes.VarSectionKind.SESSION
        assert vs.declarations[0].name == "counter"

    def test_slot(self):
        src = 'VARS {\n    x: int @3;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        vs = ast.body[0]
        assert isinstance(vs, nodes.VarSection)
        assert vs.declarations[0].slot == 3

    def test_multiple_declarations(self):
        src = 'VARS {\n    a: bool;\n    b: int;\n    c: string;\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        vs = ast.body[0]
        assert isinstance(vs, nodes.VarSection)
        assert len(vs.declarations) == 3
        assert vs.declarations[0].name == "a"
        assert vs.declarations[1].name == "b"
        assert vs.declarations[2].name == "c"

    def test_txn_and_session_in_same_program(self):
        src = ('VARS {\n    flag: bool;\n}\n'
               'SESSION_VARS {\n    counter: int;\n}\n'
               'REMAP {\n    set-debug();\n}')
        ast = _build(src)
        var_sections = [i for i in ast.body if isinstance(i, nodes.VarSection)]
        assert len(var_sections) == 2
        assert var_sections[0].scope == nodes.VarSectionKind.TXN
        assert var_sections[0].declarations[0].name == "flag"
        assert var_sections[1].scope == nodes.VarSectionKind.SESSION
        assert var_sections[1].declarations[0].name == "counter"


class TestProcedures:

    def test_basic_decl(self):
        src = 'procedure local::stamp($tag) {\n    inbound.req.X-Stamp = "$tag";\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        pd = ast.body[0]
        assert isinstance(pd, nodes.ProcedureDecl)
        assert pd.name == "local::stamp"
        assert len(pd.params) == 1
        assert pd.params[0].name == "tag"
        assert pd.params[0].default is None

    def test_default_param(self):
        src = 'procedure local::cache($ttl=300) {\n    set-debug();\n}\nREMAP {\n    set-debug();\n}'
        ast = _build(src)
        pd = ast.body[0]
        assert isinstance(pd, nodes.ProcedureDecl)
        assert pd.params[0].name == "ttl"
        assert pd.params[0].default == 300

    def test_multiple_params(self):
        src = ('procedure local::tag($key, $value="x", $count=1) {\n    set-debug();\n}\n'
               'REMAP {\n    set-debug();\n}')
        ast = _build(src)
        pd = ast.body[0]
        assert isinstance(pd, nodes.ProcedureDecl)
        assert len(pd.params) == 3
        assert pd.params[0].name == "key"
        assert pd.params[0].default is None
        assert pd.params[1].name == "value"
        assert pd.params[1].default == nodes.LiteralStringValue(raw="x")
        assert pd.params[2].name == "count"
        assert pd.params[2].default == 1

    def test_body(self):
        src = ('procedure local::multi() {\n    inbound.req.X = "a";\n'
               '    set-debug();\n}\nREMAP {\n    set-debug();\n}')
        ast = _build(src)
        pd = ast.body[0]
        assert isinstance(pd, nodes.ProcedureDecl)
        assert len(pd.body) == 2
        assert isinstance(pd.body[0], nodes.Assignment)
        assert isinstance(pd.body[1], nodes.FunctionCall)


class TestConditionExpressions:

    def _first_condition(self, source: str):
        ast = _build(source)
        return ast.body[0].body[0].condition

    def test_equality_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.X-Foo == "bar" {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.left == nodes.IdentValue(raw="inbound.req.X-Foo")
        assert cond.operator == nodes.CmpOp.EQ
        assert cond.right == nodes.LiteralStringValue(raw="bar")
        assert cond.modifiers == ()

    def test_regex_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.url.path ~ /\\.php$/ {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.MATCH
        assert isinstance(cond.right, nodes.RegexValue)

    def test_in_set(self):
        cond = self._first_condition('REMAP {\n    if inbound.url.path in ["a", "b"] {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.IN
        assert cond.right == (nodes.LiteralStringValue(raw="a"), nodes.LiteralStringValue(raw="b"))

    def test_not_in_set(self):
        cond = self._first_condition('REMAP {\n    if inbound.url.path !in ["a"] {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.NOT_IN

    def test_in_iprange(self):
        cond = self._first_condition('REMAP {\n    if inbound.ip in {10.0.0.0/8} {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.IN
        assert cond.right == (nodes.IPValue(raw="10.0.0.0/8"),)

    def test_not_in_iprange(self):
        cond = self._first_condition('REMAP {\n    if inbound.ip !in {10.0.0.0/8} {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.NOT_IN
        assert cond.right == (nodes.IPValue(raw="10.0.0.0/8"),)

    def test_modifiers(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.X-Foo == "bar" with NOCASE {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.modifiers == ("NOCASE",)

    def test_modifiers_preserve_source_casing(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.X-Foo == "bar" with nocase,Pre {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.modifiers == ("nocase", "Pre")

    def test_function_call_comparable(self):
        cond = self._first_condition('REMAP {\n    if url(true) ~ /pat/ {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert isinstance(cond.left, nodes.FunctionCall)
        assert cond.left.name == "url"
        assert cond.left.args == (True,)

    def test_bool_literal_true(self):
        cond = self._first_condition('REMAP {\n    if true {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.BoolLiteral)
        assert cond.value is True

    def test_bool_literal_false(self):
        cond = self._first_condition('REMAP {\n    if false {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.BoolLiteral)
        assert cond.value is False

    def test_ident_condition(self):
        cond = self._first_condition('REMAP {\n    if inbound.resp.All-Cache {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.IdentCondition)
        assert cond.name == "inbound.resp.All-Cache"

    def test_not_condition(self):
        cond = self._first_condition('REMAP {\n    if !inbound.resp.All-Cache {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.NotOp)
        assert isinstance(cond.operand, nodes.IdentCondition)

    def test_and_condition(self):
        cond = self._first_condition(
            'REMAP {\n    if inbound.req.X-A == "a" && inbound.req.X-B == "b" {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.AND
        assert isinstance(cond.left, nodes.Comparison)
        assert isinstance(cond.right, nodes.Comparison)

    def test_or_condition(self):
        cond = self._first_condition(
            'REMAP {\n    if inbound.req.X-A == "a" || inbound.req.X-B == "b" {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.OR

    def test_function_call_in_condition(self):
        cond = self._first_condition('REMAP {\n    if access("/tmp/bar") {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.FunctionCall)
        assert cond.name == "access"
        assert cond.args == (nodes.LiteralStringValue(raw="/tmp/bar"),)

    def test_not_tilde_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.url.path !~ /\\.jpg$/ {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.NOT_MATCH
        assert isinstance(cond.right, nodes.RegexValue)

    def test_greater_than_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.Content-Length > 1000 {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.GT
        assert cond.right == 1000

    def test_less_than_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.Content-Length < 500 {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.LT
        assert cond.right == 500

    def test_neq_comparison(self):
        cond = self._first_condition('REMAP {\n    if inbound.req.X-Foo != "bar" {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.NEQ
        assert cond.right == nodes.LiteralStringValue(raw="bar")

    def test_parenthesized_condition(self):
        cond = self._first_condition('REMAP {\n    if (inbound.req.X-Foo == "bar") {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.EQ
        assert cond.right == nodes.LiteralStringValue(raw="bar")

    def test_and_binds_tighter_than_or(self):
        # a || b && c  should parse as  a || (b && c)
        cond = self._first_condition(
            'REMAP {\n'
            '    if inbound.req.X-A == "a" || inbound.req.X-B == "b" && inbound.req.X-C == "c" {\n'
            '        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.OR
        assert isinstance(cond.left, nodes.Comparison)
        assert cond.left.left == nodes.IdentValue(raw="inbound.req.X-A")
        assert isinstance(cond.right, nodes.LogicalOp)
        assert cond.right.operator == nodes.BoolOp.AND
        assert cond.right.left.left == nodes.IdentValue(raw="inbound.req.X-B")
        assert cond.right.right.left == nodes.IdentValue(raw="inbound.req.X-C")

    def test_not_with_and(self):
        # !ident && comparison  should parse as  (!ident) && comparison
        cond = self._first_condition(
            'REMAP {\n'
            '    if !inbound.resp.All-Cache && inbound.req.X-B == "b" {\n'
            '        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.AND
        assert isinstance(cond.left, nodes.NotOp)
        assert isinstance(cond.left.operand, nodes.IdentCondition)
        assert cond.left.operand.name == "inbound.resp.All-Cache"
        assert isinstance(cond.right, nodes.Comparison)
        assert cond.right.left == nodes.IdentValue(raw="inbound.req.X-B")

    def test_not_comparison_with_or(self):
        # !(a == "x") || b == "y"  should parse as  (!(a == "x")) || (b == "y")
        cond = self._first_condition(
            'REMAP {\n'
            '    if !(inbound.req.X-A == "x") || inbound.req.X-B == "y" {\n'
            '        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.OR
        assert isinstance(cond.left, nodes.NotOp)
        assert isinstance(cond.left.operand, nodes.Comparison)
        assert cond.left.operand.left == nodes.IdentValue(raw="inbound.req.X-A")
        assert cond.left.operand.right == nodes.LiteralStringValue(raw="x")
        assert isinstance(cond.right, nodes.Comparison)
        assert cond.right.left == nodes.IdentValue(raw="inbound.req.X-B")

    def test_double_negation(self):
        cond = self._first_condition('REMAP {\n    if !!inbound.resp.All-Cache {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.NotOp)
        assert isinstance(cond.operand, nodes.NotOp)
        assert isinstance(cond.operand.operand, nodes.IdentCondition)
        assert cond.operand.operand.name == "inbound.resp.All-Cache"

    def test_not_bool_literal(self):
        cond = self._first_condition('REMAP {\n    if !false {\n        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.NotOp)
        assert isinstance(cond.operand, nodes.BoolLiteral)
        assert cond.operand.value is False

    def test_parens_override_precedence(self):
        # (a || b) && c  — parens force || to bind first
        cond = self._first_condition(
            'REMAP {\n'
            '    if (inbound.req.X-A == "a" || inbound.req.X-B == "b") && inbound.req.X-C == "c" {\n'
            '        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.AND
        assert isinstance(cond.left, nodes.LogicalOp)
        assert cond.left.operator == nodes.BoolOp.OR
        assert cond.left.left.left == nodes.IdentValue(raw="inbound.req.X-A")
        assert cond.left.right.left == nodes.IdentValue(raw="inbound.req.X-B")
        assert isinstance(cond.right, nodes.Comparison)
        assert cond.right.left == nodes.IdentValue(raw="inbound.req.X-C")

    def test_nested_parens_with_not(self):
        # !(a == "x" || b == "y") && c == "z"
        cond = self._first_condition(
            'REMAP {\n'
            '    if !(inbound.req.X-A == "x" || inbound.req.X-B == "y") && inbound.req.X-C == "z" {\n'
            '        set-debug();\n    }\n}')
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.operator == nodes.BoolOp.AND
        assert isinstance(cond.left, nodes.NotOp)
        assert isinstance(cond.left.operand, nodes.LogicalOp)
        assert cond.left.operand.operator == nodes.BoolOp.OR
        assert isinstance(cond.right, nodes.Comparison)
        assert cond.right.left == nodes.IdentValue(raw="inbound.req.X-C")


class TestIfBlocks:

    def test_simple_if(self):
        ast = _build('REMAP {\n    if true {\n        inbound.req.X = "y";\n    }\n}')
        ib = ast.body[0].body[0]
        assert isinstance(ib, nodes.IfBlock)
        assert len(ib.body) == 1
        assert ib.elif_branches == ()
        assert ib.else_body == ()

    def test_if_else(self):
        src = 'REMAP {\n    if true {\n        inbound.req.X = "a";\n    } else {\n        inbound.req.X = "b";\n    }\n}'
        ast = _build(src)
        ib = ast.body[0].body[0]
        assert len(ib.else_body) == 1

    def test_if_elif_else(self):
        src = (
            'SEND_RESPONSE {\n    if inbound.url.path == "foo" {\n'
            '        inbound.resp.X = "f";\n    } elif inbound.url.path == "bar" {\n'
            '        inbound.resp.X = "b";\n    } else {\n'
            '        inbound.resp.X = "other";\n    }\n}')
        ast = _build(src)
        ib = ast.body[0].body[0]
        assert isinstance(ib, nodes.IfBlock)
        assert len(ib.elif_branches) == 1
        assert isinstance(ib.elif_branches[0], nodes.ElifBranch)
        assert len(ib.elif_branches[0].body) == 1
        assert len(ib.else_body) == 1

    def test_multiple_elif(self):
        src = (
            'SEND_RESPONSE {\n    if inbound.url.path == "a" {\n        set-debug();\n'
            '    } elif inbound.url.path == "b" {\n        set-debug();\n'
            '    } elif inbound.url.path == "c" {\n        set-debug();\n'
            '    } else {\n        set-debug();\n    }\n}')
        ast = _build(src)
        ib = ast.body[0].body[0]
        assert len(ib.elif_branches) == 2

    def test_nested_if(self):
        src = (
            'REMAP {\n    if inbound.req.X == "a" {\n'
            '        if inbound.req.Y == "b" {\n            set-debug();\n        }\n    }\n}')
        ast = _build(src)
        outer = ast.body[0].body[0]
        assert isinstance(outer, nodes.IfBlock)
        inner = outer.body[0]
        assert isinstance(inner, nodes.IfBlock)

    def test_mixed_body(self):
        src = (
            'REMAP {\n    inbound.req.X = "before";\n'
            '    if true {\n        set-debug();\n    }\n'
            '    inbound.req.Y = "after";\n}')
        ast = _build(src)
        body = ast.body[0].body
        assert len(body) == 3
        assert isinstance(body[0], nodes.Assignment)
        assert isinstance(body[1], nodes.IfBlock)
        assert isinstance(body[2], nodes.Assignment)

    def test_empty_blocks(self):
        # Grammar permits LBRACE blockItem* RBRACE — i.e. empty if/elif/else bodies.
        src = ('REMAP {\n'
               '    if inbound.req.X-A == "a" {\n    } elif inbound.req.X-B == "b" {\n'
               '    } else {\n    }\n}')
        ast = _build(src)
        ib = ast.body[0].body[0]
        assert isinstance(ib, nodes.IfBlock)
        assert ib.body == ()
        assert len(ib.elif_branches) == 1
        assert ib.elif_branches[0].body == ()
        assert ib.else_body == ()


class TestLineNumbers:
    SRC = (
        "use test::helper\n"  # line 1
        "VARS {\n"  # line 2
        "    flag: bool;\n"  # line 3
        "}\n"  # line 4
        "procedure local::stamp($tag) {\n"  # line 5
        "    inbound.req.X-Stamp = $tag;\n"  # line 6
        "}\n"  # line 7
        "REMAP {\n"  # line 8
        '    inbound.req.X-Foo = "val";\n'  # line 9
        "    set-debug();\n"  # line 10
        "    skip-remap;\n"  # line 11
        '    if inbound.req.X-A == "a" {\n'  # line 12
        "        break;\n"  # line 13
        '    } elif inbound.req.X-B == "b" {\n'  # line 14
        '        inbound.req.X = "elif";\n'  # line 15
        "    } else {\n"  # line 16
        '        inbound.req.X = "else";\n'  # line 17
        "    }\n"  # line 18
        '    if inbound.req.X-C == "c" && inbound.req.X-D == "d" {\n'  # line 19
        "        set-debug();\n"  # line 20
        "    }\n"  # line 21
        "    if !inbound.resp.All-Cache {\n"  # line 22
        "        set-debug();\n"  # line 23
        "    }\n"  # line 24
        "    if true {\n"  # line 25
        "        set-debug();\n"  # line 26
        "    }\n"  # line 27
        "    if inbound.resp.All-Cache {\n"  # line 28
        "        set-debug();\n"  # line 29
        "    }\n"  # line 30
        "}\n"  # line 31
    )

    def setup_method(self):
        self.ast = _build(self.SRC)

    def test_use_directive(self):
        u = self.ast.body[0]
        assert isinstance(u, nodes.UseDirective)
        assert u.line == 1

    def test_var_section(self):
        vs = self.ast.body[1]
        assert isinstance(vs, nodes.VarSection)
        assert vs.line == 2

    def test_var_decl(self):
        vd = self.ast.body[1].declarations[0]
        assert isinstance(vd, nodes.VarDecl)
        assert vd.line == 3

    def test_procedure_decl(self):
        pd = self.ast.body[2]
        assert isinstance(pd, nodes.ProcedureDecl)
        assert pd.line == 5

    def test_proc_param(self):
        pp = self.ast.body[2].params[0]
        assert isinstance(pp, nodes.ProcParam)
        assert pp.line == 5

    def test_procedure_body_assignment(self):
        a = self.ast.body[2].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.line == 6

    def test_section(self):
        s = self.ast.body[3]
        assert isinstance(s, nodes.Section)
        assert s.line == 8

    def test_assignment(self):
        a = self.ast.body[3].body[0]
        assert isinstance(a, nodes.Assignment)
        assert a.line == 9

    def test_function_call(self):
        fc = self.ast.body[3].body[1]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.line == 10

    def test_standalone_operator(self):
        fc = self.ast.body[3].body[2]
        assert isinstance(fc, nodes.FunctionCall)
        assert fc.line == 11

    def test_if_block(self):
        ib = self.ast.body[3].body[3]
        assert isinstance(ib, nodes.IfBlock)
        assert ib.line == 12

    def test_comparison_in_condition(self):
        cond = self.ast.body[3].body[3].condition
        assert isinstance(cond, nodes.Comparison)
        assert cond.line == 12

    def test_break(self):
        brk = self.ast.body[3].body[3].body[0]
        assert isinstance(brk, nodes.Break)
        assert brk.line == 13

    def test_elif_branch(self):
        eb = self.ast.body[3].body[3].elif_branches[0]
        assert isinstance(eb, nodes.ElifBranch)
        assert eb.line == 14

    def test_elif_condition(self):
        cond = self.ast.body[3].body[3].elif_branches[0].condition
        assert isinstance(cond, nodes.Comparison)
        assert cond.line == 14

    def test_logical_op(self):
        cond = self.ast.body[3].body[4].condition
        assert isinstance(cond, nodes.LogicalOp)
        assert cond.line == 19

    def test_not_op(self):
        cond = self.ast.body[3].body[5].condition
        assert isinstance(cond, nodes.NotOp)
        assert cond.line == 22

    def test_bool_literal(self):
        cond = self.ast.body[3].body[6].condition
        assert isinstance(cond, nodes.BoolLiteral)
        assert cond.line == 25

    def test_ident_condition(self):
        cond = self.ast.body[3].body[7].condition
        assert isinstance(cond, nodes.IdentCondition)
        assert cond.line == 28


class TestRealConfigs:

    def test_nested_ifs_from_test_data(self):
        """Validates AST for tests/data/conds/nested-ifs.input.txt pattern."""
        src = '''VARS {
    bool_0: bool;
    bool_1: bool;
    bool_2: bool;
}

REMAP {
    if inbound.req.X-Foo == "bar" {
        inbound.req.X-Hello = "there";
        if inbound.req.X-Fie == "fie" {
            inbound.req.X-first = "1";
            if bool_0 || (bool_1 && bool_2) {
                inbound.req.X-Parsed = "more";
            } else {
                inbound.req.X-Parsed = "yes";
            }
        } elif inbound.req.X-Fum == "bar" {
            inbound.req.X-Parsed = "no";
        } else {
            inbound.req.X-More = "yes";
        }
    } elif inbound.req.X-Foo == "foo" with NOCASE,PRE {
        inbound.req.X-Nocase = "foo";
    } else {
        inbound.req.X-Something = "no-bar";
    }
}'''
        ast = _build(src)
        sections = [i for i in ast.body if isinstance(i, nodes.Section)]
        assert len(sections) == 1
        s = sections[0]
        assert s.type == "REMAP"

        # Top-level if block
        outer = s.body[0]
        assert isinstance(outer, nodes.IfBlock)

        # Body: assignment + nested if
        assert isinstance(outer.body[0], nodes.Assignment)
        assert isinstance(outer.body[1], nodes.IfBlock)
        middle = outer.body[1]

        # Middle if has elif and else
        assert len(middle.elif_branches) == 1
        assert len(middle.else_body) == 1

        # Deepest nested if (3 levels)
        inner = middle.body[1]
        assert isinstance(inner, nodes.IfBlock)
        assert isinstance(inner.condition, nodes.LogicalOp)
        assert inner.condition.operator == nodes.BoolOp.OR

        # Outer elif has modifiers
        assert len(outer.elif_branches) == 1
        elif_cond = outer.elif_branches[0].condition
        assert isinstance(elif_cond, nodes.Comparison)
        assert elif_cond.modifiers == ("NOCASE", "PRE")

    def test_http_cntl_booleans(self):
        """Validates value coercion for boolean-like assignments."""
        src = '''SEND_RESPONSE {
    http.cntl.TXN_DEBUG = true;
    http.cntl.LOGGING = FALSE;
}'''
        ast = _build(src)
        body = ast.body[0].body
        assert body[0].value is True
        assert body[1].value is False

    def test_ip_range_condition(self):
        """Validates IP range handling from tests/data/conds/ip.input.txt."""
        src = '''SEND_REQUEST {
    if inbound.ip in {192.168.0.0/16, 10.0.0.0/8} {
        set-debug();
    }
}'''
        ast = _build(src)
        cond = ast.body[0].body[0].condition
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.IN
        assert len(cond.right) == 2

    def test_set_membership_with_modifier(self):
        """From tests/data/conds/in-sets.input.txt."""
        src = '''REMAP {
    if inbound.url.path in ["php", "php3", "php4"] with EXT {
        inbound.req.X-Is-PHP = "yes";
    }
}'''
        ast = _build(src)
        cond = ast.body[0].body[0].condition
        assert isinstance(cond, nodes.Comparison)
        assert cond.operator == nodes.CmpOp.IN
        assert cond.right == (nodes.LiteralStringValue(raw="php"), nodes.LiteralStringValue(raw="php3"), nodes.LiteralStringValue(raw="php4"))
        assert cond.modifiers == ("EXT",)

    def test_debug_pattern_for_lint_rules(self):
        """Validates the exact pattern the no-debug lint rule will match."""
        src = '''REMAP {
    set-debug();
    http.cntl.TXN_DEBUG = true;
    inbound.req.X-Foo = "test";
}'''
        ast = _build(src)
        body = ast.body[0].body

        # set-debug() function call
        assert isinstance(body[0], nodes.FunctionCall)
        assert body[0].name == "set-debug"

        # TXN_DEBUG assignment with True
        assert isinstance(body[1], nodes.Assignment)
        assert body[1].target == nodes.Target.from_dotted("http.cntl.TXN_DEBUG")
        assert body[1].value is True

        # Regular assignment (not flagged)
        assert isinstance(body[2], nodes.Assignment)
        assert body[2].target.namespace == "inbound.req"
