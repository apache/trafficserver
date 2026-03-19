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

from hrw4u.errors import ErrorCollector, SymbolResolutionError
from hrw4u.common import create_parse_tree, generate_output
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from hrw4u.visitor_base import BaseHRWVisitor
from hrw4u.sandbox import SandboxConfig, PolicySets
from hrw4u.symbols import SymbolResolver
from hrw4u.interning import intern_keyword, intern_section, intern_lsp_string
import pytest


class TestSandboxValidation:
    """Unit tests for sandbox config validation error paths."""

    def test_load_set_non_list(self):
        from hrw4u.sandbox import _load_set

        with pytest.raises(ValueError, match="must be a list"):
            _load_set({"sections": "not-a-list"}, "sections", "deny")

    def test_policy_sets_non_dict(self):
        with pytest.raises(ValueError, match="must be a mapping"):
            PolicySets.load("not-a-dict", "deny")

    def test_policy_sets_unknown_key(self):
        with pytest.raises(ValueError, match="Unknown keys"):
            PolicySets.load({"sections": [], "bogus": []}, "deny")

    def test_policy_sets_unknown_language(self):
        with pytest.raises(ValueError, match="Unknown language constructs"):
            PolicySets.load({"language": ["nonexistent"]}, "deny")

    def test_policy_sets_unknown_modifier(self):
        with pytest.raises(ValueError, match="Unknown modifiers"):
            PolicySets.load({"modifiers": ["BOGUS"]}, "deny")

    def test_policy_sets_is_active(self):
        empty = PolicySets()
        assert not empty.is_active
        active = PolicySets(sections=frozenset(["REMAP"]))
        assert active.is_active

    def test_sandbox_config_is_active(self):
        empty = SandboxConfig.empty()
        assert not empty.is_active
        active = SandboxConfig(message="", deny=PolicySets(sections=frozenset(["REMAP"])), warn=PolicySets())
        assert active.is_active

    def test_sandbox_load_non_dict_sandbox(self, tmp_path):
        f = tmp_path / "bad.yaml"
        f.write_text("sandbox: not-a-dict\n")
        with pytest.raises(ValueError, match="must be a mapping"):
            SandboxConfig.load(f)

    def test_sandbox_load_non_dict_deny(self, tmp_path):
        f = tmp_path / "bad.yaml"
        f.write_text("sandbox:\n  deny: not-a-dict\n")
        with pytest.raises(ValueError, match="sandbox.deny must be a mapping"):
            SandboxConfig.load(f)

    def test_sandbox_load_non_dict_warn(self, tmp_path):
        f = tmp_path / "bad.yaml"
        f.write_text("sandbox:\n  warn: not-a-dict\n")
        with pytest.raises(ValueError, match="sandbox.warn must be a mapping"):
            SandboxConfig.load(f)

    def test_sandbox_load_missing_key(self, tmp_path):
        f = tmp_path / "bad.yaml"
        f.write_text("not_sandbox: true\n")
        with pytest.raises(ValueError, match="top-level 'sandbox' key"):
            SandboxConfig.load(f)

    def test_sandbox_load_deny_warn_overlap(self, tmp_path):
        f = tmp_path / "bad.yaml"
        f.write_text("sandbox:\n  deny:\n    sections:\n      - REMAP\n  warn:\n    sections:\n      - REMAP\n")
        with pytest.raises(ValueError, match="overlap"):
            SandboxConfig.load(f)

    def test_is_matched_prefix(self):
        from hrw4u.sandbox import _is_matched

        assert _is_matched("request_header.Host", frozenset(["request_header."]))
        assert not _is_matched("response_header.Host", frozenset(["request_header."]))


class TestBaseHRWVisitorMethods:
    """Unit tests for BaseHRWVisitor methods not covered by integration tests."""

    def test_increment_decrement_cond_indent(self):
        v = BaseHRWVisitor()
        assert v.cond_indent == 0
        v.increment_cond_indent()
        assert v.cond_indent == 1
        v.decrement_cond_indent()
        assert v.cond_indent == 0
        v.decrement_cond_indent()
        assert v.cond_indent == 0

    def test_emit_line_and_emit_statement(self):
        v = BaseHRWVisitor()
        v.emit_line("hello", 0)
        v.emit_statement("world")
        assert v.output == ["hello", "world"]

    def test_debug_enter_with_args(self):
        v = BaseHRWVisitor(debug=True)
        v.debug_enter("test_method", "arg1", "arg2")
        v.debug_exit("test_method", "result_value")

    def test_debug_method(self):
        v = BaseHRWVisitor(debug=True)
        v.debug("test message")

    def test_normalize_empty_string_condition(self):
        v = BaseHRWVisitor()
        assert v._normalize_empty_string_condition('foo != ""') == "foo"
        assert v._normalize_empty_string_condition('foo == ""') == "!foo"
        assert v._normalize_empty_string_condition("foo > 5") == "foo > 5"

    def test_build_condition_connector(self):
        from hrw4u.states import CondState

        v = BaseHRWVisitor()
        state = CondState()
        assert v._build_condition_connector(state) == "&&"
        state.and_or = True
        assert v._build_condition_connector(state) == "||"
        assert v._build_condition_connector(state, is_last_term=True) == "&&"

    def test_reconstruct_redirect_args(self):
        v = BaseHRWVisitor()
        assert v._reconstruct_redirect_args(["301"]) == ["301"]
        assert v._reconstruct_redirect_args(["301", '"http://"', '"example.com"']) == ["301", "http://example.com"]


class TestInterning:
    """Unit tests for string interning utilities."""

    def test_intern_keyword_known(self):
        result = intern_keyword("if")
        assert result == "if"

    def test_intern_keyword_unknown(self):
        result = intern_keyword("not_a_keyword_xyz")
        assert result == "not_a_keyword_xyz"

    def test_intern_section(self):
        assert intern_section("REMAP") == "REMAP"
        assert intern_section("UNKNOWN_SECTION") == "UNKNOWN_SECTION"

    def test_intern_lsp_string(self):
        assert intern_lsp_string("markdown") == "markdown"
        assert intern_lsp_string("unknown_lsp") == "unknown_lsp"


class TestSymbolResolverErrors:
    """Unit tests for SymbolResolver error paths."""

    def test_resolve_assignment_unknown_symbol(self):
        resolver = SymbolResolver()
        with pytest.raises(SymbolResolutionError, match="Unknown assignment"):
            resolver.resolve_assignment("nonexistent_symbol", "value")

    def test_resolve_add_assignment_unsupported(self):
        resolver = SymbolResolver()
        with pytest.raises(SymbolResolutionError, match="not supported"):
            resolver.resolve_add_assignment("nonexistent_symbol", "value")

    def test_resolve_function_unknown(self):
        resolver = SymbolResolver()
        with pytest.raises(SymbolResolutionError, match="Unknown function"):
            resolver.resolve_function("nonexistent_func", [])

    def test_resolve_statement_func_unknown(self):
        resolver = SymbolResolver()
        with pytest.raises(SymbolResolutionError, match="Unknown statement function"):
            resolver.resolve_statement_func("nonexistent_func", [])

    def test_get_variable_suggestions(self):
        resolver = SymbolResolver()
        result = resolver.get_variable_suggestions("req_heade")
        assert isinstance(result, list)


class TestCreateParseTreeErrors:
    """Unit tests for create_parse_tree exception handling paths."""

    def test_syntax_error_collected(self):
        tree, parser_obj, ec = create_parse_tree(
            "REMAP { completely invalid @@@ syntax ;;; }",
            "<test>",
            hrw4uLexer,
            hrw4uParser,
            "test",
            collect_errors=True,
            max_errors=10)
        assert ec is not None
        assert ec.has_errors()

    def test_general_exception_collected(self):
        """Trigger the generic Exception handler in create_parse_tree."""
        from unittest.mock import patch

        with patch.object(hrw4uParser, 'program', side_effect=RuntimeError("boom")):
            tree, _, ec = create_parse_tree("x = 1", "<test>", hrw4uLexer, hrw4uParser, "test", collect_errors=True, max_errors=10)
        assert tree is None
        assert ec is not None
        assert ec.has_errors()
        assert "boom" in str(ec.errors[0]).lower()


class TestGenerateOutput:
    """Unit tests for generate_output paths."""

    def _make_args(self, **overrides):
        from types import SimpleNamespace

        defaults = dict(ast=False, debug=False, no_comments=False, output=None)
        defaults.update(overrides)
        return SimpleNamespace(**defaults)

    def test_with_extra_kwargs(self, capsys):
        text = 'REMAP { inbound.conn.dscp = 17; }'
        tree, parser_obj, _ = create_parse_tree(text, "<test>", hrw4uLexer, hrw4uParser, "test")
        args = self._make_args()
        ec = ErrorCollector()
        generate_output(tree, parser_obj, HRW4UVisitor, "<test>", args, ec, extra_kwargs={"preserve_comments": False})
        captured = capsys.readouterr()
        assert "set-conn-dscp" in captured.out

    def test_visitor_exception_collected(self, capsys):
        """Visitor that raises should collect error when error_collector is present."""

        class BrokenVisitor:

            def __init__(self, **kw):
                pass

            def visit(self, tree):
                raise RuntimeError("visitor broke")

        text = 'REMAP { inbound.conn.dscp = 17; }'
        tree, parser_obj, _ = create_parse_tree(text, "<test>", hrw4uLexer, hrw4uParser, "test")
        args = self._make_args()
        ec = ErrorCollector()
        generate_output(tree, parser_obj, BrokenVisitor, "<test>", args, ec)
        assert ec.has_errors()
        assert "Visitor error" in str(ec.errors[0])


class TestInverseSymbolResolver:
    """Unit tests for InverseSymbolResolver methods."""

    def _resolver(self):
        from u4wrh.hrw_symbols import InverseSymbolResolver

        return InverseSymbolResolver()

    def test_negate_inequality(self):
        r = self._resolver()
        assert r.negate_expression('foo != "bar"') == 'foo != "bar"'

    def test_negate_regex(self):
        r = self._resolver()
        assert r.negate_expression("x ~ /pat/") == "x !~ /pat/"

    def test_negate_in(self):
        r = self._resolver()
        assert r.negate_expression("x in [a, b]") == "x !in [a, b]"

    def test_negate_comparison(self):
        r = self._resolver()
        result = r.negate_expression("x > 5")
        assert result == "!(x > 5)"

    def test_negate_function_call(self):
        r = self._resolver()
        result = r.negate_expression("foo()")
        assert result == "!foo()"

    def test_negate_simple_ident(self):
        r = self._resolver()
        assert r.negate_expression("flag") == "!flag"

    def test_negate_complex_expression(self):
        r = self._resolver()
        result = r.negate_expression("a && b")
        assert result == "!(a && b)"

    def test_convert_set_parens_to_brackets(self):
        r = self._resolver()
        assert r.convert_set_to_brackets("(a, b, c)") == "[a, b, c]"

    def test_convert_set_already_brackets(self):
        r = self._resolver()
        assert r.convert_set_to_brackets("[a, b]") == "[a, b]"

    def test_convert_set_invalid(self):
        r = self._resolver()
        assert r.convert_set_to_brackets("{a, b}") == "{a, b}"

    def test_format_iprange(self):
        r = self._resolver()
        assert r.format_iprange("{10.0.0.0/8, 192.168.0.0/16}") == "{10.0.0.0/8, 192.168.0.0/16}"

    def test_format_iprange_invalid(self):
        r = self._resolver()
        assert r.format_iprange("[bad]") == "[bad]"

    def test_handle_state_tag_unknown_type(self):
        r = self._resolver()
        with pytest.raises(SymbolResolutionError, match="Unknown state type"):
            r._handle_state_tag("STATE-BOGUS", "0")

    def test_handle_state_tag_invalid_index(self):
        r = self._resolver()
        with pytest.raises(SymbolResolutionError, match="Invalid index"):
            r._handle_state_tag("STATE-FLAG", "abc")

    def test_handle_state_tag_missing_payload(self):
        r = self._resolver()
        with pytest.raises(SymbolResolutionError, match="Missing index"):
            r._handle_state_tag("STATE-FLAG", None)

    def test_handle_ip_tag_unknown(self):
        r = self._resolver()
        result, _ = r._handle_ip_tag("UNKNOWN-IP-THING")
        assert result is None

    def test_rewrite_inline_booleans(self):
        r = self._resolver()
        assert r._rewrite_inline_percents("TRUE", None) == "true"
        assert r._rewrite_inline_percents("FALSE", None) == "false"

    def test_rewrite_inline_numeric(self):
        r = self._resolver()
        assert r._rewrite_inline_percents("42", None) == "42"

    def test_rewrite_inline_percent_block(self):
        r = self._resolver()
        result = r._rewrite_inline_percents("%{STATUS}", None)
        assert "{" in result

    def test_rewrite_inline_fallback_quoted(self):
        r = self._resolver()
        result = r._rewrite_inline_percents("some-value", None)
        assert result.startswith('"')

    def test_get_var_declarations_empty(self):
        r = self._resolver()
        assert r.get_var_declarations() == ([], [])

    def test_get_var_declarations_after_state_tag(self):
        r = self._resolver()
        r._handle_state_tag("STATE-FLAG", "0")
        txn_decls, ssn_decls = r.get_var_declarations()
        assert len(txn_decls) == 1
        assert "bool" in txn_decls[0]

    def test_percent_to_ident_invalid(self):
        r = self._resolver()
        with pytest.raises(SymbolResolutionError, match="Invalid"):
            r.percent_to_ident_or_func("not-a-percent", None)

    def test_op_to_hrw4u_unknown_operator(self):
        r = self._resolver()
        from hrw4u.states import OperatorState

        with pytest.raises(SymbolResolutionError, match="Unknown operator"):
            r.op_to_hrw4u("totally-fake-op", [], None, OperatorState())

    def test_op_to_hrw4u_state_var_missing_args(self):
        r = self._resolver()
        from hrw4u.states import OperatorState

        with pytest.raises(SymbolResolutionError, match="Missing arguments"):
            r.op_to_hrw4u("set-state-flag", [], None, OperatorState())

    def test_op_to_hrw4u_state_var_invalid_index(self):
        r = self._resolver()
        from hrw4u.states import OperatorState

        with pytest.raises(SymbolResolutionError, match="Invalid index"):
            r.op_to_hrw4u("set-state-flag", ["abc", "true"], None, OperatorState())

    def test_parse_percent_block_invalid(self):
        r = self._resolver()
        tag, payload = r.parse_percent_block("not-a-percent")
        assert tag == "not-a-percent"
        assert payload is None

    def test_should_lowercase_url_suffix(self):
        r = self._resolver()
        assert r._should_lowercase_suffix("CLIENT-URL", "inbound.url.") is True

    def test_should_lowercase_now(self):
        r = self._resolver()
        assert r._should_lowercase_suffix("NOW", "now.") is True

    def test_should_not_lowercase_header(self):
        r = self._resolver()
        assert r._should_lowercase_suffix("HEADER", "request_header.") is False


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
