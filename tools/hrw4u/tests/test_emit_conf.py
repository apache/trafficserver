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

import yaml

from hrw4u.ast_visitor import ASTVisitor
from hrw4u.common import compile_to_conf
from hrw4u.emit_conf import ConfEmitter
from hrw4u.errors import ErrorCollector
from hrw4u.sandbox import SandboxConfig
from hrw4u.sema import Sema
from utils import parse_input_text


def _emit(source: str, **kwargs) -> list[str]:
    _, tree = parse_input_text(source)
    ast = ASTVisitor(filename="<test>").visit(tree)
    s = Sema(**kwargs)
    s.source_map.add("<test>", source)
    return ConfEmitter().emit(s.analyze(ast))


class TestSectionFraming:

    def test_hook_then_indented_statement(self):
        assert _emit('REMAP {\n    set-debug();\n}') == ["cond %{REMAP_PSEUDO_HOOK} [AND]", "    set-debug"]

    def test_statement_run_shares_one_hook(self):
        assert _emit('REMAP {\n    set-debug();\n    no-op();\n}') == [
            "cond %{REMAP_PSEUDO_HOOK} [AND]", "    set-debug", "    no-op"
        ]

    def test_blank_line_between_sections(self):
        out = _emit('REMAP {\n    set-debug();\n}\nSEND_RESPONSE {\n    no-op();\n}')
        assert out[2] == ""
        assert out[3] == "cond %{SEND_RESPONSE_HDR_HOOK} [AND]"


class TestStatements:

    def test_header_set(self):
        assert _emit('REMAP {\n    inbound.req.X-Foo = "bar";\n}')[1] == '    set-header X-Foo "bar"'

    def test_empty_value_removes(self):
        assert _emit('REMAP {\n    inbound.req.X-Foo = "";\n}')[1] == "    rm-header X-Foo"

    def test_plus_equals_adds(self):
        assert _emit('REMAP {\n    inbound.req.X-Foo += "bar";\n}')[1] == '    add-header X-Foo "bar"'

    def test_exact_operator(self):
        assert _emit('REMAP {\n    inbound.status = 404;\n}')[1] == "    set-status 404"

    def test_variable_assignment(self):
        out = _emit('VARS {\n    flag: bool;\n}\nREMAP {\n    flag = true;\n}')
        assert out[1] == "    set-state-flag 0 true"

    def test_variable_to_variable_assignment_resolves_the_rhs(self):
        """A bare IdentValue RHS naming a variable resolves to that variable's VarRef, not to
        RawArg(name), which would render the RHS's own name."""
        out = _emit('VARS {\n    bool_0: bool;\n    bool_1: bool;\n}\nREMAP {\n    bool_1 = bool_0;\n}')
        assert out[1] == "    set-state-flag 1 %{STATE-FLAG:0}"

    def test_break_is_a_terminal_no_op(self):
        out = _emit('REMAP {\n    if inbound.status > 399 {\n        break;\n    }\n}')
        assert "    no-op [L]" in out

    def test_keep_query_gets_the_invert_flag(self):
        assert _emit('REMAP {\n    keep_query("a");\n}')[1] == '    rm-destination QUERY "a" [I]'

    def test_interpolated_string(self):
        assert _emit('REMAP {\n    inbound.req.X-M = "m={inbound.method}";\n}')[1] == '    set-header X-M "m=%{METHOD}"'

    def test_comment_is_preserved_unindented(self):
        assert _emit('REMAP {\n    # why\n    set-debug();\n}')[1] == "# why"

    def test_comment_after_a_statement_is_also_unindented(self):
        out = _emit('REMAP {\n    set-debug();\n    # why\n    no-op();\n}')
        assert out == ["cond %{REMAP_PSEUDO_HOOK} [AND]", "    set-debug", "# why", "    no-op"]

    def test_comment_inside_a_block_is_indented(self):
        out = _emit('REMAP {\n    if inbound.status > 399 {\n        # inner\n        set-debug();\n    }\n}')
        assert "    # inner" in out

    def test_vars_comment_leads_the_output(self):
        out = _emit('VARS {\n    # note\n    flag: bool;\n}\nREMAP {\n    set-debug();\n}')
        assert out[0] == "# note"
        assert out[1] == ""
        assert out[2] == "cond %{REMAP_PSEUDO_HOOK} [AND]"

    def test_comment_is_dropped_when_disabled(self):
        assert "# why" not in _emit('REMAP {\n    # why\n    set-debug();\n}', preserve_comments=False)


class TestConditions:

    def test_simple_if(self):
        assert _emit('REMAP {\n    if inbound.status > 399 {\n        set-debug();\n    }\n}') == [
            "cond %{REMAP_PSEUDO_HOOK} [AND]", "cond %{STATUS} >399", "    set-debug"
        ]

    def test_not_equal_sets_the_not_flag(self):
        out = _emit('REMAP {\n    if inbound.method != "GET" {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{METHOD} ="GET" [NOT]'

    def test_and_chain(self):
        out = _emit('REMAP {\n    if inbound.status > 399 && inbound.status < 500 {\n        set-debug();\n    }\n}')
        assert out[1] == "cond %{STATUS} >399 [AND]"
        assert out[2] == "cond %{STATUS} <500"

    def test_or_chain(self):
        out = _emit('REMAP {\n    if inbound.status > 399 || inbound.status < 200 {\n        set-debug();\n    }\n}')
        assert out[1] == "cond %{STATUS} >399 [OR]"

    def test_group(self):
        out = _emit(
            'REMAP {\n    if (inbound.status > 399 || inbound.status < 200) && inbound.method == "GET" {\n'
            '        set-debug();\n    }\n}')
        assert out == [
            "cond %{REMAP_PSEUDO_HOOK} [AND]",
            "cond %{GROUP}",
            "    cond %{STATUS} >399 [OR]",
            "    cond %{STATUS} <200",
            "cond %{GROUP:END} [AND]",
            'cond %{METHOD} ="GET"',
            "    set-debug",
        ]

    def test_else_and_elif(self):
        out = _emit(
            'REMAP {\n    if inbound.status > 399 {\n        set-debug();\n    } elif inbound.status > 299 {\n'
            '        no-op();\n    } else {\n        counter("c");\n    }\n}')
        assert out == [
            "cond %{REMAP_PSEUDO_HOOK} [AND]",
            "cond %{STATUS} >399",
            "    set-debug",
            "elif",
            "    cond %{STATUS} >299",
            "        no-op",
            "else",
            '    counter "c"',
        ]

    def test_nested_if_uses_if_endif(self):
        out = _emit(
            'REMAP {\n    if inbound.status > 399 {\n        if inbound.method == "GET" {\n'
            '            set-debug();\n        }\n    }\n}')
        assert out == [
            "cond %{REMAP_PSEUDO_HOOK} [AND]",
            "cond %{STATUS} >399",
            "    if",
            '        cond %{METHOD} ="GET"',
            "            set-debug",
            "    endif",
        ]

    def test_bare_prefix_condition_is_an_existence_test(self):
        out = _emit('REMAP {\n    if inbound.req.X-Foo {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{CLIENT-HEADER:X-Foo} ="" [NOT]'

    def test_nocase_modifier(self):
        out = _emit('REMAP {\n    if inbound.req.Host == "x" with NOCASE {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{CLIENT-HEADER:Host} ="x" [NOCASE]'

    def test_function_condition(self):
        out = _emit('REMAP {\n    if access("/tmp/bar") {\n        set-debug();\n    }\n}')
        assert out[1] == "cond %{ACCESS:/tmp/bar}"

    def test_zero_arg_function_comparable_is_wrapped(self):
        """FUNCTION_MAP targets are bare ("CACHE"), so they are wrapped in %{} even with no arg."""
        out = _emit('READ_RESPONSE {\n    if cache() == "hit-stale" {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{CACHE} ="hit-stale"'

    def test_true_literal(self):
        out = _emit('REMAP {\n    if true {\n        set-debug();\n    }\n}')
        assert out[1] == "cond %{TRUE}"

    def test_not_equal_with_nocase(self):
        out = _emit('REMAP {\n    if inbound.req.Host != "x" with NOCASE {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{CLIENT-HEADER:Host} ="x" [NOT,NOCASE]'

    def test_in_set_modifier_order_follows_cond_state_not_source(self):
        out = _emit('REMAP {\n    if inbound.req.Path in ["a","b"] with EXT,NOCASE {\n        set-debug();\n    }\n}')
        assert out[1] == 'cond %{CLIENT-HEADER:Path} ("a","b") [NOCASE,EXT]'


class TestCompileToConf:

    def test_a_run_that_reported_an_error_returns_no_lines(self, tmp_path):
        # Rendered anyway, the denied operand would vanish and leave its `!` on the next
        # condition: `cond %{METHOD} ="GET" [NOT]`.
        config = tmp_path / "sandbox.yaml"
        config.write_text(yaml.safe_dump({"sandbox": {"deny": {"functions": ["access"]}}}))
        source = 'REMAP {\n    if !access("/tmp/x") && inbound.method == "GET" {\n        set-debug();\n    }\n}\n'
        collector = ErrorCollector()
        _, tree = parse_input_text(source)
        lines = compile_to_conf(tree, "<test>", source, error_collector=collector, sandbox=SandboxConfig.load(str(config)))
        assert collector.has_errors()
        assert lines == []
