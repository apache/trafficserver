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
"""
Tests for common.py functions called in-process for coverage visibility.

The existing test_cli.py tests these paths via subprocess, which coverage
cannot track. These tests call the same functions directly so coverage
can see them.
"""
from __future__ import annotations

import io
from types import SimpleNamespace

import pytest
from hrw4u.common import (
    create_base_parser,
    create_parse_tree,
    fatal,
    generate_output,
    process_input,
    run_main,
)
from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor

# ---------------------------------------------------------------------------
# Approach A: Unit tests for individual building-block functions
# ---------------------------------------------------------------------------


class TestFatal:
    """Tests for the fatal() helper."""

    def test_fatal_exits_with_code_1(self, capsys):
        with pytest.raises(SystemExit) as exc_info:
            fatal("something broke")
        assert exc_info.value.code == 1
        assert "something broke" in capsys.readouterr().err


class TestCreateBaseParser:
    """Tests for create_base_parser()."""

    def test_returns_parser_and_group(self):
        parser, group = create_base_parser("test description")
        assert parser is not None
        assert group is not None

    def test_parser_has_expected_arguments(self):
        parser, _ = create_base_parser("test")
        args = parser.parse_args(["--ast", "--debug", "--stop-on-error"])
        assert args.ast is True
        assert args.debug is True
        assert args.stop_on_error is True

    def test_parser_defaults(self):
        parser, _ = create_base_parser("test")
        args = parser.parse_args([])
        assert args.ast is False
        assert args.debug is False
        assert args.stop_on_error is False


class TestProcessInput:
    """Tests for process_input()."""

    def test_stdin_returns_default_filename(self):
        fake_stdin = io.StringIO("hello world")
        fake_stdin.name = "<stdin>"
        content, filename = process_input(fake_stdin)
        assert content == "hello world"
        assert filename == "<stdin>"

    def test_file_input_returns_real_filename(self, tmp_path):
        p = tmp_path / "test.hrw4u"
        p.write_text("set-header X-Foo bar")
        with open(p, "r", encoding="utf-8") as f:
            content, filename = process_input(f)
        assert content == "set-header X-Foo bar"
        assert str(p) in filename


class TestCreateParseTree:
    """Tests for create_parse_tree()."""

    def test_valid_input_with_error_collection(self):
        tree, parser_obj, errors = create_parse_tree(
            'REMAP { no-op(); }', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        assert tree is not None
        assert errors is not None
        assert not errors.has_errors()

    def test_valid_input_without_error_collection(self):
        tree, parser_obj, errors = create_parse_tree(
            'REMAP { no-op(); }', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=False)
        assert tree is not None
        assert errors is None

    def test_invalid_input_collects_errors(self):
        tree, parser_obj, errors = create_parse_tree(
            '{{{{ totally broken syntax !!! }}}}', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        assert errors is not None
        assert errors.has_errors()


class TestGenerateOutput:
    """Tests for generate_output()."""

    def test_normal_output(self, capsys):
        tree, parser_obj, errors = create_parse_tree(
            'REMAP { no-op(); }', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        args = SimpleNamespace(ast=False, debug=False, no_comments=False)
        generate_output(tree, parser_obj, HRW4UVisitor, "<test>", args, errors)
        out = capsys.readouterr().out
        assert "no-op" in out

    def test_ast_output(self, capsys):
        tree, parser_obj, errors = create_parse_tree(
            'REMAP { no-op(); }', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        args = SimpleNamespace(ast=True, debug=False)
        generate_output(tree, parser_obj, HRW4UVisitor, "<test>", args, errors)
        out = capsys.readouterr().out
        assert "program" in out.lower() or "(" in out

    def test_ast_mode_with_parse_errors(self, capsys):
        tree, parser_obj, errors = create_parse_tree(
            '{{{{ broken }}}}', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        args = SimpleNamespace(ast=True, debug=False)
        generate_output(tree, parser_obj, HRW4UVisitor, "<test>", args, errors)
        captured = capsys.readouterr()
        assert errors.has_errors()

    def test_error_collector_summary_on_errors(self, capsys):
        tree, parser_obj, errors = create_parse_tree(
            '{{{{ broken }}}}', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        args = SimpleNamespace(ast=False, debug=False, no_comments=False)
        generate_output(tree, parser_obj, HRW4UVisitor, "<test>", args, errors)
        err = capsys.readouterr().err
        assert "error" in err.lower() or "Error" in err

    def test_ast_mode_tree_none_with_errors(self, capsys):
        """When tree is None and errors exist, AST mode prints fallback message."""
        errors = ErrorCollector()
        errors.add_error(Hrw4uSyntaxError("<test>", 1, 0, "parse failed", "bad"))
        args = SimpleNamespace(ast=True, debug=False)
        generate_output(None, None, HRW4UVisitor, "<test>", args, errors)
        out = capsys.readouterr().out
        assert "Parse tree not available" in out

    def test_error_collector_exits_on_parse_failure(self, capsys):
        """When tree is None and errors exist in non-AST mode, should exit(1)."""
        errors = ErrorCollector()
        errors.add_error(Hrw4uSyntaxError("<test>", 1, 0, "parse failed", "bad"))
        args = SimpleNamespace(ast=False, debug=False, no_comments=False)
        with pytest.raises(SystemExit) as exc_info:
            generate_output(None, None, HRW4UVisitor, "<test>", args, errors)
        assert exc_info.value.code == 1

    def test_visitor_exception_collected(self, capsys):
        """When visitor.visit() raises, error is collected and reported."""

        class BrokenVisitor:

            def __init__(self, **kwargs):
                pass

            def visit(self, tree):
                exc = RuntimeError("visitor exploded")
                exc.add_note("hint: check input")
                raise exc

        tree, parser_obj, errors = create_parse_tree(
            'REMAP { no-op(); }', "<test>", hrw4uLexer, hrw4uParser, "hrw4u", collect_errors=True)
        args = SimpleNamespace(ast=False, debug=False, no_comments=False)
        generate_output(tree, parser_obj, BrokenVisitor, "<test>", args, errors)
        err = capsys.readouterr().err
        assert "visitor exploded" in err.lower() or "Visitor error" in err


# ---------------------------------------------------------------------------
# Approach B: run_main() called in-process via monkeypatch
# ---------------------------------------------------------------------------


class TestRunMain:
    """Tests for run_main() covering the CLI orchestration code."""

    def _run(self, monkeypatch, capsys, argv, stdin_text=None):
        """Helper to invoke run_main() with patched sys.argv and optional stdin."""
        monkeypatch.setattr("sys.argv", ["hrw4u"] + argv)
        if stdin_text is not None:
            monkeypatch.setattr("sys.stdin", io.StringIO(stdin_text))
        run_main("HRW4U test", hrw4uLexer, hrw4uParser, HRW4UVisitor, "hrw4u", "hrw", "Produce header_rewrite output")
        return capsys.readouterr()

    def test_stdin_to_stdout(self, monkeypatch, capsys):
        captured = self._run(monkeypatch, capsys, [], stdin_text='REMAP { no-op(); }')
        assert "no-op" in captured.out

    def test_single_file_to_stdout(self, monkeypatch, capsys, tmp_path):
        p = tmp_path / "test.hrw4u"
        p.write_text('REMAP { inbound.req.X-Hello = "world"; }')
        captured = self._run(monkeypatch, capsys, [str(p)])
        assert "X-Hello" in captured.out

    def test_multiple_files_with_separator(self, monkeypatch, capsys, tmp_path):
        f1 = tmp_path / "a.hrw4u"
        f1.write_text('REMAP { no-op(); }')
        f2 = tmp_path / "b.hrw4u"
        f2.write_text('REMAP { inbound.req.X-B = "val"; }')
        captured = self._run(monkeypatch, capsys, [str(f1), str(f2)])
        assert "# ---" in captured.out
        assert "no-op" in captured.out
        assert "X-B" in captured.out

    def test_bulk_input_output_pairs(self, monkeypatch, capsys, tmp_path):
        inp = tmp_path / "in.hrw4u"
        inp.write_text('REMAP { no-op(); }')
        out = tmp_path / "out.conf"
        self._run(monkeypatch, capsys, [f"{inp}:{out}"])
        assert out.exists()
        assert "no-op" in out.read_text()

    def test_bulk_nonexistent_input(self, monkeypatch, capsys, tmp_path):
        out = tmp_path / "out.conf"
        with pytest.raises(SystemExit) as exc_info:
            self._run(monkeypatch, capsys, [f"/no/such/file.hrw4u:{out}"])
        assert exc_info.value.code == 1

    def test_mixed_format_error(self, monkeypatch, capsys, tmp_path):
        f1 = tmp_path / "a.hrw4u"
        f1.write_text('REMAP { no-op(); }')
        out = tmp_path / "out.conf"
        with pytest.raises(SystemExit) as exc_info:
            self._run(monkeypatch, capsys, [str(f1), f"{f1}:{out}"])
        assert exc_info.value.code == 1

    def test_ast_mode(self, monkeypatch, capsys, tmp_path):
        p = tmp_path / "test.hrw4u"
        p.write_text('REMAP { no-op(); }')
        captured = self._run(monkeypatch, capsys, ["--ast", str(p)])
        assert "program" in captured.out.lower() or "(" in captured.out

    def test_no_comments_flag(self, monkeypatch, capsys):
        captured = self._run(monkeypatch, capsys, ["--no-comments"], stdin_text='REMAP { no-op(); }')
        assert "no-op" in captured.out

    def test_stop_on_error_flag(self, monkeypatch, capsys):
        captured = self._run(monkeypatch, capsys, ["--stop-on-error"], stdin_text='REMAP { no-op(); }')
        assert "no-op" in captured.out


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
