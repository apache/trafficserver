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
    ThrowingErrorListener, hrw4u_error, CollectingErrorListener
from hrw4u.validation import Validator, ValidatorChain
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


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
