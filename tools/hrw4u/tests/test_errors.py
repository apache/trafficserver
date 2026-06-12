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

from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError, SymbolResolutionError, \
    ThrowingErrorListener, hrw4u_error, CollectingErrorListener, Warning
from hrw4u.formatters import FORMATTERS, JSON_SCHEMA_VERSION, JSONFormatter, MarkdownFormatter, PlainTextFormatter
from hrw4u.validation import Validator, ValidatorChain
import json
import pytest


class TestThrowingErrorListener:
    """Unit tests for ThrowingErrorListener."""

    def test_raises_syntax_error(self):
        listener = ThrowingErrorListener("test.hrw4u")

        class FakeLexer:
            inputStream = None

        with pytest.raises(Hrw4uSyntaxError) as exc_info:
            listener.syntaxError(FakeLexer(), None, 1, 5, "unexpected token", None)

        err = exc_info.value
        assert err.filename == "test.hrw4u"
        assert err.line == 1
        assert err.column == 5

    def test_extracts_source_line_from_lexer(self):
        listener = ThrowingErrorListener("test.hrw4u")

        class FakeInputStream:
            strdata = "first line\nsecond line\nthird line"

        class FakeLexer:
            inputStream = FakeInputStream()

        with pytest.raises(Hrw4uSyntaxError) as exc_info:
            listener.syntaxError(FakeLexer(), None, 2, 0, "bad token", None)

        assert exc_info.value.source_line == "second line"

    def test_extracts_source_line_from_parser(self):
        listener = ThrowingErrorListener("test.hrw4u")

        class FakeInputStream:
            strdata = "line one\nline two"

        class FakeTokenSource:
            inputStream = FakeInputStream()

        class FakeStream:
            tokenSource = FakeTokenSource()

        class FakeParser:

            def getInputStream(self):
                return FakeStream()

        with pytest.raises(Hrw4uSyntaxError) as exc_info:
            listener.syntaxError(FakeParser(), None, 1, 3, "parse error", None)

        assert exc_info.value.source_line == "line one"

    def test_falls_back_on_broken_recognizer(self):
        listener = ThrowingErrorListener("test.hrw4u")

        class BrokenRecognizer:
            pass

        with pytest.raises(Hrw4uSyntaxError) as exc_info:
            listener.syntaxError(BrokenRecognizer(), None, 1, 0, "error", None)

        assert exc_info.value.source_line == ""


class TestHrw4uErrorFunction:
    """Unit tests for hrw4u_error helper."""

    def test_passthrough_syntax_error(self):
        original = Hrw4uSyntaxError("f.hrw4u", 1, 0, "msg", "line")
        assert hrw4u_error("f.hrw4u", None, original) is original

    def test_no_context(self):
        exc = ValueError("something broke")
        result = hrw4u_error("f.hrw4u", None, exc)
        assert result.line == 0
        assert result.column == 0

    def test_with_context(self):

        class FakeInputStream:
            strdata = "some code here"

        class FakeToken:
            line = 1
            column = 5

            def getInputStream(self):
                return FakeInputStream()

        class FakeCtx:
            start = FakeToken()

        exc = ValueError("bad value")
        result = hrw4u_error("f.hrw4u", FakeCtx(), exc)
        assert result.line == 1
        assert result.column == 5
        assert result.source_line == "some code here"

    def test_with_context_broken_input_stream(self):

        class FakeToken:
            line = 1
            column = 0

            def getInputStream(self):
                raise RuntimeError("broken")

        class FakeCtx:
            start = FakeToken()

        exc = ValueError("oops")
        result = hrw4u_error("f.hrw4u", FakeCtx(), exc)
        assert result.source_line == ""

    def test_preserves_notes(self):
        exc = ValueError("base error")
        exc.add_note("hint: try X")
        result = hrw4u_error("f.hrw4u", None, exc)
        assert hasattr(result, '__notes__')
        assert "hint: try X" in result.__notes__


class TestErrorCollectorEdgeCases:
    """Additional edge case tests for ErrorCollector."""

    def test_empty_summary(self):
        ec = ErrorCollector()
        assert ec.get_error_summary() == "No errors found."

    def test_error_with_notes_in_summary(self):
        ec = ErrorCollector()
        err = Hrw4uSyntaxError("f.hrw4u", 1, 0, "bad", "code")
        err.add_note("hint: fix it")
        ec.add_error(err)
        summary = ec.get_error_summary()
        assert "hint: fix it" in summary


class TestCollectingErrorListener:
    """Unit tests for CollectingErrorListener."""

    def test_collects_errors(self):
        ec = ErrorCollector()
        listener = CollectingErrorListener("test.hrw4u", ec)

        class FakeLexer:
            inputStream = None

        listener.syntaxError(FakeLexer(), None, 1, 0, "bad token", None)
        assert ec.has_errors()
        assert ec.errors[0].line == 1

    def test_extracts_source_from_lexer(self):
        ec = ErrorCollector()
        listener = CollectingErrorListener("test.hrw4u", ec)

        class FakeInputStream:
            strdata = "the source line"

        class FakeLexer:
            inputStream = FakeInputStream()

        listener.syntaxError(FakeLexer(), None, 1, 5, "error", None)
        assert ec.errors[0].source_line == "the source line"


class TestValidatorChainUnits:
    """Unit tests for ValidatorChain convenience methods."""

    def test_arg_at_valid(self):
        chain = ValidatorChain()
        validator = Validator.nbit_int(8)
        chain.arg_at(1, validator)
        chain(["foo", "42"])

    def test_arg_at_missing_index(self):
        chain = ValidatorChain()
        validator = Validator.nbit_int(8)
        chain.arg_at(5, validator)
        with pytest.raises(SymbolResolutionError, match="Missing argument"):
            chain(["foo"])

    def test_nbit_int_valid(self):
        v = Validator.nbit_int(8)
        v("0")
        v("255")

    def test_nbit_int_out_of_range(self):
        v = Validator.nbit_int(8)
        with pytest.raises(SymbolResolutionError, match="8-bit"):
            v("256")

    def test_nbit_int_not_integer(self):
        v = Validator.nbit_int(8)
        with pytest.raises(SymbolResolutionError, match="Expected an integer"):
            v("abc")

    def test_range_valid(self):
        v = Validator.range(1, 100)
        v("1")
        v("50")
        v("100")

    def test_range_out_of_range(self):
        v = Validator.range(1, 100)
        with pytest.raises(SymbolResolutionError, match="range"):
            v("0")
        with pytest.raises(SymbolResolutionError, match="range"):
            v("101")

    def test_range_not_integer(self):
        v = Validator.range(1, 100)
        with pytest.raises(SymbolResolutionError, match="Expected an integer"):
            v("abc")

    def test_validate_nbit_int8_via_chain(self):
        chain = ValidatorChain()
        chain.nbit_int(8)
        chain(["42"])

    def test_validate_nbit_int8_out_of_range(self):
        chain = ValidatorChain()
        chain.nbit_int(8)
        with pytest.raises(SymbolResolutionError, match="8-bit"):
            chain(["256"])

    def test_validate_nbit_int16_not_integer(self):
        chain = ValidatorChain()
        chain.nbit_int(16)
        with pytest.raises(SymbolResolutionError, match="Expected an integer"):
            chain(["notanumber"])

    def test_set_format_valid(self):
        v = Validator.set_format()
        v("[a, b, c]")
        v("(single)")

    def test_set_format_invalid(self):
        v = Validator.set_format()
        with pytest.raises(SymbolResolutionError, match="Set must be enclosed"):
            v("not-a-set")

    def test_iprange_format_valid(self):
        v = Validator.iprange_format()
        v("{10.0.0.0/8, 192.168.0.0/16}")
        v("{::1/128}")

    def test_iprange_format_invalid(self):
        v = Validator.iprange_format()
        with pytest.raises(SymbolResolutionError, match="IP range"):
            v("not-an-ip-range")

    def test_regex_pattern_valid(self):
        v = Validator.regex_pattern()
        v("/foo.*/")
        v("/^start$/")

    def test_regex_pattern_invalid(self):
        v = Validator.regex_pattern()
        with pytest.raises(SymbolResolutionError, match="[Rr]egex"):
            v("/[invalid/")

    def test_regex_pattern_empty(self):
        v = Validator.regex_pattern()
        with pytest.raises(SymbolResolutionError):
            v("")

    def test_conditional_arg_validation_valid(self):
        validator = Validator.conditional_arg_validation({"status_code": frozenset(["200", "301", "404"])})
        validator(["status_code", "200"])

    def test_conditional_arg_validation_invalid_value(self):
        validator = Validator.conditional_arg_validation({"status_code": frozenset(["200", "301", "404"])})
        with pytest.raises(SymbolResolutionError, match="Invalid value"):
            validator(["status_code", "999"])

    def test_conditional_arg_validation_unknown_field(self):
        validator = Validator.conditional_arg_validation({"status_code": frozenset(["200", "301", "404"])})
        with pytest.raises(SymbolResolutionError, match="Unknown"):
            validator(["unknown_field", "200"])

    def test_percent_block_valid(self):
        v = Validator.percent_block()
        v("%{TAG:value}")
        v("%{SIMPLE}")

    def test_percent_block_invalid(self):
        v = Validator.percent_block()
        with pytest.raises(SymbolResolutionError, match="percent block"):
            v("not-percent-block")

    def test_needs_quotes(self):
        assert Validator.needs_quotes("has space")
        assert Validator.needs_quotes("")
        assert not Validator.needs_quotes("simple")

    def test_quote_if_needed(self):
        assert Validator.quote_if_needed("simple") == "simple"
        assert Validator.quote_if_needed("has space") == '"has space"'

    @pytest.mark.parametrize(
        "value,expected",
        [
            ('"X-Foo"', "X-Foo"),
            ('"X Foo"', '"X Foo"'),
            ("X-Foo", "X-Foo"),
            ('""', '""'),
            ('"@internal"', '"@internal"'),
            ('"1foo"', '"1foo"'),
        ],
    )
    def test_unquote_if_ident(self, value, expected):
        assert Validator.unquote_if_ident(value) == expected


class TestPlainTextFormatterParity:
    """The plain formatter must preserve current CLI output byte-for-byte."""

    def test_registry_has_expected_formats(self):
        assert set(FORMATTERS.keys()) == {"plain", "json", "markdown"}

    def test_empty_returns_no_errors_found(self):
        ec = ErrorCollector(formatter=PlainTextFormatter())
        assert ec.get_error_summary() == "No errors found."

    def test_single_error_omits_found_preamble(self):
        """A single error should not be prefixed with 'Found 1 error:'."""
        ec = ErrorCollector(formatter=PlainTextFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 1, 4, "oops", "foo bar")
        ec.add_error(err)
        out = ec.get_error_summary()
        assert not out.startswith("Found")
        assert out.startswith("f.hrw4u:1:4: error: oops")
        assert "   1 | foo bar" in out

    def test_multiple_errors_include_found_preamble(self):
        """Two or more errors keep the 'Found N errors:' summary line."""
        ec = ErrorCollector(formatter=PlainTextFormatter())
        ec.add_error(Hrw4uSyntaxError("f.hrw4u", 1, 0, "a", ""))
        ec.add_error(Hrw4uSyntaxError("f.hrw4u", 2, 0, "b", ""))
        assert ec.get_error_summary().startswith("Found 2 errors:\n")

    def test_at_limit_marker(self):
        ec = ErrorCollector(max_errors=2, formatter=PlainTextFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 1, 0, "x", "")
        ec.add_error(err)
        ec.add_error(err)
        assert "(stopped after 2 errors)" in ec.get_error_summary()

    def test_sandbox_message_appended(self):
        ec = ErrorCollector(formatter=PlainTextFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 1, 0, "x", "")
        ec.add_error(err)
        ec.set_sandbox_message("sandbox blocked thing")
        assert ec.get_error_summary().endswith("sandbox blocked thing")

    def test_default_formatter_is_plain(self):
        """ErrorCollector() with no formatter must produce the legacy output."""
        legacy = ErrorCollector()
        custom = ErrorCollector(formatter=PlainTextFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 2, 3, "m", "src")
        err.add_note("hint")
        legacy.add_error(err)
        custom.add_error(err)
        assert legacy.get_error_summary() == custom.get_error_summary()


class TestJSONFormatter:
    """JSON output is the stable contract for downstream UIs (edgeconf, etc.)."""

    def _collect(self) -> ErrorCollector:
        ec = ErrorCollector(formatter=JSONFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 3, 4, "unexpected '('", "if foo ( {")
        err.add_note("hint: try X")
        ec.add_error(err)
        ec.add_warning(Warning(filename="f.hrw4u", line=7, column=0, message="deprecated", source_line="old;"))
        return ec

    def test_output_is_valid_json(self):
        payload = json.loads(self._collect().get_error_summary())
        assert payload["version"] == JSON_SCHEMA_VERSION

    def test_error_fields_are_preserved(self):
        payload = json.loads(self._collect().get_error_summary())
        err = payload["errors"][0]
        assert err["filename"] == "f.hrw4u"
        assert err["line"] == 3
        assert err["column"] == 4
        assert err["severity"] == "error"
        assert err["message"] == "unexpected '('"
        assert err["source_line"] == "if foo ( {"
        assert err["notes"] == ["hint: try X"]

    def test_warning_severity_and_message(self):
        payload = json.loads(self._collect().get_error_summary())
        w = payload["warnings"][0]
        assert w["severity"] == "warning"
        assert w["message"] == "deprecated"
        assert w["notes"] == []

    def test_summary_counts_and_truncation(self):
        payload = json.loads(self._collect().get_error_summary())
        assert payload["summary"]["error_count"] == 1
        assert payload["summary"]["warning_count"] == 1
        assert payload["summary"]["truncated"] is False
        assert payload["summary"]["max_errors"] == 5

    def test_truncated_flag_flips_at_limit(self):
        ec = ErrorCollector(max_errors=2, formatter=JSONFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 1, 0, "x", "")
        ec.add_error(err)
        ec.add_error(err)
        payload = json.loads(ec.get_error_summary())
        assert payload["summary"]["truncated"] is True
        assert payload["summary"]["max_errors"] == 2

    def test_sandbox_message_is_top_level(self):
        ec = self._collect()
        ec.set_sandbox_message("sandbox blocked x")
        payload = json.loads(ec.get_error_summary())
        assert payload["sandbox_message"] == "sandbox blocked x"

    def test_empty_collector_still_emits_valid_schema(self):
        ec = ErrorCollector(formatter=JSONFormatter())
        payload = json.loads(ec.get_error_summary())
        assert payload["errors"] == []
        assert payload["warnings"] == []
        assert payload["sandbox_message"] is None

    def test_single_line_output_for_ndjson(self):
        out = self._collect().get_error_summary()
        assert "\n" not in out, "JSON output must be single-line for NDJSON streaming"


class TestMarkdownFormatter:
    """Markdown output is pure markdown — no ANSI, no colors."""

    def _collect(self) -> ErrorCollector:
        ec = ErrorCollector(formatter=MarkdownFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 3, 4, "unexpected '('", "if foo ( {")
        err.add_note("hint: try X")
        ec.add_error(err)
        return ec

    def test_has_top_level_heading(self):
        assert self._collect().get_error_summary().startswith("## hrw4u:")

    def test_error_heading_includes_location(self):
        assert "### Error — `f.hrw4u:3:4`" in self._collect().get_error_summary()

    def test_contains_fenced_code_block_with_caret(self):
        md = self._collect().get_error_summary()
        assert "```" in md
        assert "   3 | if foo ( {" in md
        assert "^" in md

    def test_notes_render_as_blockquotes(self):
        assert "> hint: try X" in self._collect().get_error_summary()

    def test_empty_collector_friendly_message(self):
        ec = ErrorCollector(formatter=MarkdownFormatter())
        assert ec.get_error_summary() == "_No errors found._"

    def test_no_source_line_skips_code_block(self):
        ec = ErrorCollector(formatter=MarkdownFormatter())
        ec.add_error(Hrw4uSyntaxError("f.hrw4u", 0, 0, "file not found", ""))
        md = ec.get_error_summary()
        assert "```" not in md
        assert "file not found" in md

    def test_at_limit_marker(self):
        ec = ErrorCollector(max_errors=2, formatter=MarkdownFormatter())
        err = Hrw4uSyntaxError("f.hrw4u", 1, 0, "x", "src")
        ec.add_error(err)
        ec.add_error(err)
        assert "Stopped after 2 errors" in ec.get_error_summary()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
