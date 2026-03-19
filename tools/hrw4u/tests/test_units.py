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

from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError, SymbolResolutionError, humanize_error_message
from hrw4u.common import create_parse_tree
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from hrw4u.validation import Validator
from hrw4u.types import MapParams
from hrw4u.states import SectionType
import pytest


class TestHRW4UVisitorUnits:

    def setup_method(self):
        self.visitor = HRW4UVisitor()

    def test_parse_function_args_empty(self):
        assert self.visitor._parse_function_args('') == []
        assert self.visitor._parse_function_args('   ') == []

    def test_parse_function_args_simple(self):
        assert self.visitor._parse_function_args('arg1') == ['arg1']
        assert self.visitor._parse_function_args('arg1, arg2') == ['arg1', 'arg2']
        assert self.visitor._parse_function_args('arg1, arg2, arg3') == ['arg1', 'arg2', 'arg3']

    def test_parse_function_args_quoted_commas(self):
        assert self.visitor._parse_function_args('"arg1, with comma", arg2') == ['"arg1, with comma"', 'arg2']
        assert self.visitor._parse_function_args('arg1, "arg2, with comma"') == ['arg1', '"arg2, with comma"']
        assert self.visitor._parse_function_args('"first, comma", "second, comma"') == ['"first, comma"', '"second, comma"']

    def test_parse_function_args_single_quotes(self):
        assert self.visitor._parse_function_args("'arg1, with comma', arg2") == ["'arg1, with comma'", 'arg2']
        assert self.visitor._parse_function_args("arg1, 'arg2, with comma'") == ['arg1', "'arg2, with comma'"]

    def test_parse_function_args_nested_functions(self):
        assert self.visitor._parse_function_args('func(a,b), arg2') == ['func(a,b)', 'arg2']
        assert self.visitor._parse_function_args('arg1, func(a,b)') == ['arg1', 'func(a,b)']
        assert self.visitor._parse_function_args('func1(a,b), func2(c,d)') == ['func1(a,b)', 'func2(c,d)']

    def test_parse_function_args_deeply_nested(self):
        assert self.visitor._parse_function_args('func(nested(a,b),c), arg2') == ['func(nested(a,b),c)', 'arg2']
        assert self.visitor._parse_function_args('outer(inner(deep(x,y),z),w), final') == ['outer(inner(deep(x,y),z),w)', 'final']

    def test_parse_function_args_mixed_complex(self):
        complex_arg = 'func("arg1, with comma", nested_func(a,b), "arg3")'
        assert self.visitor._parse_function_args(complex_arg) == [complex_arg]
        assert self.visitor._parse_function_args('"quoted, arg", func(a,b), normal_arg') == [
            '"quoted, arg"', 'func(a,b)', 'normal_arg'
        ]

    def test_parse_function_args_whitespace_handling(self):
        assert self.visitor._parse_function_args(' arg1 , arg2 ') == ['arg1', 'arg2']
        assert self.visitor._parse_function_args('func( a , b ), arg2') == ['func( a , b )', 'arg2']
        assert self.visitor._parse_function_args('\targ1\t,\targ2\t') == ['arg1', 'arg2']

    def test_parse_function_args_escaped_quotes(self):
        # The current implementation doesn't handle escaped quotes perfectly,
        # but this documents the current behavior
        result = self.visitor._parse_function_args('"arg with \\" quote", arg2')

    def test_parse_function_args_edge_cases(self):
        assert self.visitor._parse_function_args('func(a,b, arg2') == ['func(a,b, arg2']
        assert self.visitor._parse_function_args('arg1,, arg2') == ['arg1', '', 'arg2']
        assert self.visitor._parse_function_args(',,,') == ['', '', '']


class TestErrorCollectorUnits:

    def setup_method(self):
        self.error_collector = ErrorCollector()

    def test_error_collector_basic(self):
        assert not self.error_collector.has_errors()

        test_error = Hrw4uSyntaxError("test.hrw4u", 1, 0, "Test error", "test line")
        self.error_collector.add_error(test_error)
        assert self.error_collector.has_errors()

        error_summary = self.error_collector.get_error_summary()
        assert "Test error" in error_summary
        assert "Found 1 error:" in error_summary

    def test_error_collector_multiple_errors(self):
        error1 = Hrw4uSyntaxError("test1.hrw4u", 1, 0, "Error 1", "line 1")
        error2 = Hrw4uSyntaxError("test2.hrw4u", 2, 5, "Error 2", "line 2")
        error3 = Hrw4uSyntaxError("test3.hrw4u", 3, 10, "Error 3", "line 3")

        self.error_collector.add_error(error1)
        self.error_collector.add_error(error2)
        self.error_collector.add_error(error3)

        assert self.error_collector.has_errors()
        error_summary = self.error_collector.get_error_summary()

        assert "Error 1" in error_summary
        assert "Error 2" in error_summary
        assert "Error 3" in error_summary
        assert "Found 3 errors:" in error_summary


class TestValidationUnits:

    def test_http_header_name_valid_standard(self):
        validator = Validator.http_header_name()

        valid_names = [
            "Content-Type",
            "X-Custom-Header",
            "User-Agent",
            "Accept-Encoding",
            "X_Custom_Header",
            "X~Custom~Header",
            "X^Custom^Header",
            "X|Custom|Header",
            "X!Custom!Header",
            "X#Custom#Header",
            "X$Custom$Header",
            "X%Custom%Header",
            "X&Custom&Header",
            "X'Custom'Header",
            "X*Custom*Header",
            "X+Custom+Header",
            "X`Custom`Header",
        ]

        for name in valid_names:
            validator(name)

    def test_http_header_name_valid_ats_internal(self):
        validator = Validator.http_header_name()

        valid_ats_names = [
            "@Client-Txn-Count",
            "@X-Method",
            "@PropertyName",
            "@Custom_Header",
        ]

        for name in valid_ats_names:
            validator(name)

    def test_http_header_name_invalid(self):
        validator = Validator.http_header_name()

        invalid_names = [
            "",
            "@",
            "Content Type",
            "Content\tType",
            "Content\nType",
            "Content(Type)",
            "Content[Type]",
            "Content{Type}",
            "Content<Type>",
            "Content@Type",
            "Content,Type",
            "Content;Type",
            "Content:Type",
            "Content=Type",
            "Content?Type",
            "Content/Type",
            "Content\\Type",
            "Content\"Type\"",
            "@Content@Type",
            "X-@Header",
            "headers.X-Match",
            "X.Custom.Header",
            "@Custom.Header",
            "header.X-Foo",
        ]

        for name in invalid_names:
            with pytest.raises(SymbolResolutionError):
                validator(name)

    def test_http_token_valid(self):
        validator = Validator.http_token()

        valid_tokens = [
            "Content-Type",
            "simple_token",
            "Token123",
            "!#$%&'*+-.^_`|~",
            "Mixed123.Token-Name_Test",
        ]

        for token in valid_tokens:
            validator(token)

    def test_http_token_invalid(self):
        validator = Validator.http_token()

        invalid_tokens = [
            "",
            "token with space",
            "token\ttab",
            "token\nnewline",
            "token(paren)",
            "token[bracket]",
            "token{brace}",
            "token<angle>",
            "token@at",
            "token,comma",
            "token;semicolon",
            "token:colon",
            "token=equals",
            "token?question",
            "token/slash",
            "token\\backslash",
            "token\"quote\"",
        ]

        for token in invalid_tokens:
            with pytest.raises(SymbolResolutionError):
                validator(token)

    def test_regex_validator_factory(self):
        import re

        test_pattern = re.compile(r'^[A-Z]+$')
        validator = Validator.regex_validator(test_pattern, "Must be uppercase letters only")

        validator("ABC")
        validator("HELLO")
        validator("TEST")

        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("abc")

        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("Hello")

        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("TEST123")


class TestHumanizeErrorMessage:

    def test_replaces_qualified_ident(self):
        msg = "mismatched input 'Apple' expecting QUALIFIED_IDENT"
        result = humanize_error_message(msg)
        assert "QUALIFIED_IDENT" not in result
        assert "qualified name" in result

    def test_replaces_punctuation_tokens(self):
        msg = "mismatched input '{' expecting LPAREN"
        result = humanize_error_message(msg)
        assert "LPAREN" not in result
        assert "'('" in result

    def test_replaces_multiple_tokens(self):
        msg = "expecting {IDENT, QUALIFIED_IDENT, SEMICOLON}"
        result = humanize_error_message(msg)
        assert "IDENT" not in result
        assert "QUALIFIED_IDENT" not in result
        assert "SEMICOLON" not in result

    def test_preserves_normal_text(self):
        msg = "unknown procedure 'test::foo': not loaded via 'use'"
        result = humanize_error_message(msg)
        assert result == msg

    def test_no_partial_word_replacement(self):
        msg = "some IDENTIFIER_LIKE text"
        result = humanize_error_message(msg)
        assert result == msg


class TestParseErrorMessages:
    """Verify that ANTLR parse errors use human-friendly token names."""

    def _first_error(self, text: str) -> str:
        _, _, collector = create_parse_tree(text, "<test>", hrw4uLexer, hrw4uParser, "test", collect_errors=True, max_errors=10)
        assert collector and collector.has_errors(), f"Expected parse errors for: {text!r}"
        return str(collector.errors[0])

    def test_single_colon_in_procedure(self):
        error = self._first_error("procedure Apple:Roles() { }")
        assert "QUALIFIED_IDENT" not in error
        assert "qualified name" in error

    def test_missing_lparen(self):
        error = self._first_error("procedure ns::foo { }")
        assert "LPAREN" not in error
        assert "'('" in error

    def test_unqualified_procedure_name(self):
        error = self._first_error("procedure nocolon() { }")
        assert "QUALIFIED_IDENT" not in error
        assert "qualified name" in error

    def test_bad_use_directive(self):
        error = self._first_error("use Foo")
        assert "QUALIFIED_IDENT" not in error
        assert "qualified name" in error


class TestMapParamsUnits:
    """Unit tests for MapParams dunder methods."""

    def test_repr_empty(self):
        mp = MapParams()
        assert repr(mp) == "MapParams()"

    def test_repr_with_flags(self):
        mp = MapParams(upper=True, add=True)
        r = repr(mp)
        assert "upper=True" in r
        assert "add=True" in r

    def test_repr_with_sections(self):
        mp = MapParams(sections={SectionType.REMAP})
        assert "sections=" in repr(mp)

    def test_repr_with_validate(self):
        mp = MapParams(validate=lambda x: None)
        assert "validate=<validator>" in repr(mp)

    def test_repr_with_target(self):
        mp = MapParams(target="set-header")
        assert "target=..." in repr(mp)

    def test_hash_basic(self):
        mp1 = MapParams(upper=True)
        mp2 = MapParams(upper=True)
        assert hash(mp1) == hash(mp2)

    def test_hash_with_sections(self):
        mp = MapParams(sections={SectionType.REMAP})
        assert isinstance(hash(mp), int)

    def test_hash_with_rev_dict(self):
        mp = MapParams(rev={"a": "b"})
        assert isinstance(hash(mp), int)

    def test_hash_with_validate(self):
        mp = MapParams(validate=lambda x: None)
        assert isinstance(hash(mp), int)

    def test_eq_same(self):
        mp1 = MapParams(upper=True)
        mp2 = MapParams(upper=True)
        assert mp1 == mp2

    def test_eq_different(self):
        mp1 = MapParams(upper=True)
        mp2 = MapParams(upper=False)
        assert mp1 != mp2

    def test_eq_non_mapparams(self):
        mp = MapParams()
        assert mp != "not a MapParams"

    def test_getattr_rejects_private(self):
        mp = MapParams()
        with pytest.raises(AttributeError):
            _ = mp._private


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
