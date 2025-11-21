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
Unit tests for internal methods and helper functions.

This module tests individual methods and components that might not be
fully exercised by the integration tests, providing focused testing
for internal implementation details.
"""

from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError, SymbolResolutionError
from hrw4u.visitor import HRW4UVisitor
from hrw4u.validation import Validator
import pytest
import sys
import os


class TestHRW4UVisitorUnits:
    """Unit tests for HRW4U internal methods."""

    def setup_method(self):
        """Set up test fixtures."""
        self.visitor = HRW4UVisitor(None, None, None)

    def test_parse_function_args_empty(self):
        """Test _parse_function_args with empty input."""
        assert self.visitor._parse_function_args('') == []
        assert self.visitor._parse_function_args('   ') == []

    def test_parse_function_args_simple(self):
        """Test _parse_function_args with simple arguments."""
        assert self.visitor._parse_function_args('arg1') == ['arg1']
        assert self.visitor._parse_function_args('arg1, arg2') == ['arg1', 'arg2']
        assert self.visitor._parse_function_args('arg1, arg2, arg3') == ['arg1', 'arg2', 'arg3']

    def test_parse_function_args_quoted_commas(self):
        """Test _parse_function_args with quotes containing commas."""
        assert self.visitor._parse_function_args('"arg1, with comma", arg2') == ['"arg1, with comma"', 'arg2']
        assert self.visitor._parse_function_args('arg1, "arg2, with comma"') == ['arg1', '"arg2, with comma"']
        assert self.visitor._parse_function_args('"first, comma", "second, comma"') == ['"first, comma"', '"second, comma"']

    def test_parse_function_args_single_quotes(self):
        """Test _parse_function_args with single quotes containing commas."""
        assert self.visitor._parse_function_args("'arg1, with comma', arg2") == ["'arg1, with comma'", 'arg2']
        assert self.visitor._parse_function_args("arg1, 'arg2, with comma'") == ['arg1', "'arg2, with comma'"]

    def test_parse_function_args_nested_functions(self):
        """Test _parse_function_args with nested function calls."""
        assert self.visitor._parse_function_args('func(a,b), arg2') == ['func(a,b)', 'arg2']
        assert self.visitor._parse_function_args('arg1, func(a,b)') == ['arg1', 'func(a,b)']
        assert self.visitor._parse_function_args('func1(a,b), func2(c,d)') == ['func1(a,b)', 'func2(c,d)']

    def test_parse_function_args_deeply_nested(self):
        """Test _parse_function_args with deeply nested parentheses."""
        assert self.visitor._parse_function_args('func(nested(a,b),c), arg2') == ['func(nested(a,b),c)', 'arg2']
        assert self.visitor._parse_function_args('outer(inner(deep(x,y),z),w), final') == ['outer(inner(deep(x,y),z),w)', 'final']

    def test_parse_function_args_mixed_complex(self):
        """Test _parse_function_args with complex mixed cases."""
        complex_arg = 'func("arg1, with comma", nested_func(a,b), "arg3")'
        assert self.visitor._parse_function_args(complex_arg) == [complex_arg]
        assert self.visitor._parse_function_args('"quoted, arg", func(a,b), normal_arg') == [
            '"quoted, arg"', 'func(a,b)', 'normal_arg'
        ]

    def test_parse_function_args_whitespace_handling(self):
        """Test _parse_function_args with various whitespace patterns."""
        assert self.visitor._parse_function_args(' arg1 , arg2 ') == ['arg1', 'arg2']
        assert self.visitor._parse_function_args('func( a , b ), arg2') == ['func( a , b )', 'arg2']
        assert self.visitor._parse_function_args('\targ1\t,\targ2\t') == ['arg1', 'arg2']

    def test_parse_function_args_escaped_quotes(self):
        """Test _parse_function_args with escaped quotes (basic test)."""
        # Note: The current implementation doesn't handle escaped quotes perfectly,
        # but this documents the current behavior
        result = self.visitor._parse_function_args('"arg with \\" quote", arg2')

    def test_parse_function_args_edge_cases(self):
        """Test _parse_function_args with edge cases."""
        assert self.visitor._parse_function_args('func(a,b, arg2') == ['func(a,b, arg2']
        assert self.visitor._parse_function_args('arg1,, arg2') == ['arg1', '', 'arg2']
        assert self.visitor._parse_function_args(',,,') == ['', '', '']


class TestErrorCollectorUnits:
    """Unit tests for ErrorCollector internal methods."""

    def setup_method(self):
        """Set up test fixtures."""
        self.error_collector = ErrorCollector()

    def test_error_collector_basic(self):
        """Test basic ErrorCollector functionality."""
        assert not self.error_collector.has_errors()

        test_error = Hrw4uSyntaxError("test.hrw4u", 1, 0, "Test error", "test line")
        self.error_collector.add_error(test_error)
        assert self.error_collector.has_errors()

        error_summary = self.error_collector.get_error_summary()
        assert "Test error" in error_summary
        assert "Found 1 error:" in error_summary

    def test_error_collector_multiple_errors(self):
        """Test ErrorCollector with multiple errors."""
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
    """Unit tests for validation functions."""

    def test_http_header_name_valid_standard(self):
        """Test http_header_name with valid standard RFC 7230 header names."""
        validator = Validator.http_header_name()

        valid_names = [
            "Content-Type",
            "X-Custom-Header",
            "User-Agent",
            "Accept-Encoding",
            "X_Custom_Header",  # Underscores allowed
            "X~Custom~Header",  # Tildes allowed
            "X^Custom^Header",  # Carets allowed
            "X|Custom|Header",  # Pipes allowed
            "X!Custom!Header",  # Exclamation marks allowed
            "X#Custom#Header",  # Hash marks allowed
            "X$Custom$Header",  # Dollar signs allowed
            "X%Custom%Header",  # Percent signs allowed
            "X&Custom&Header",  # Ampersands allowed
            "X'Custom'Header",  # Single quotes allowed
            "X*Custom*Header",  # Asterisks allowed
            "X+Custom+Header",  # Plus signs allowed
            "X`Custom`Header",  # Backticks allowed
        ]

        for name in valid_names:
            # Should not raise an exception
            validator(name)

    def test_http_header_name_valid_ats_internal(self):
        """Test http_header_name with valid ATS internal headers (@ prefix)."""
        validator = Validator.http_header_name()

        valid_ats_names = [
            "@Client-Txn-Count",
            "@X-Method",
            "@PropertyName",
            "@Custom_Header",
        ]

        for name in valid_ats_names:
            # Should not raise an exception
            validator(name)

    def test_http_header_name_invalid(self):
        """Test http_header_name with invalid header names."""
        validator = Validator.http_header_name()

        invalid_names = [
            "",  # Empty name
            "@",  # Just @ alone
            "Content Type",  # Space not allowed
            "Content\tType",  # Tab not allowed
            "Content\nType",  # Newline not allowed
            "Content(Type)",  # Parentheses not allowed
            "Content[Type]",  # Brackets not allowed
            "Content{Type}",  # Braces not allowed
            "Content<Type>",  # Angle brackets not allowed
            "Content@Type",  # @ not allowed in middle
            "Content,Type",  # Comma not allowed
            "Content;Type",  # Semicolon not allowed
            "Content:Type",  # Colon not allowed
            "Content=Type",  # Equals not allowed
            "Content?Type",  # Question mark not allowed
            "Content/Type",  # Forward slash not allowed
            "Content\\Type",  # Backslash not allowed
            "Content\"Type\"",  # Quotes not allowed
            "@Content@Type",  # @ not allowed after first position
            "X-@Header",  # @ not allowed after first position
            "headers.X-Match",  # Dots not allowed (hrw4u restriction)
            "X.Custom.Header",  # Dots not allowed (hrw4u restriction)
            "@Custom.Header",  # Dots not allowed even in ATS headers
            "header.X-Foo",  # Dots not allowed (the specific case mentioned)
        ]

        for name in invalid_names:
            with pytest.raises(SymbolResolutionError):
                validator(name)

    def test_http_token_valid(self):
        """Test http_token with valid tokens."""
        validator = Validator.http_token()

        valid_tokens = [
            "Content-Type",
            "simple_token",
            "Token123",
            "!#$%&'*+-.^_`|~",  # All allowed special chars
            "Mixed123.Token-Name_Test",
        ]

        for token in valid_tokens:
            # Should not raise an exception
            validator(token)

    def test_http_token_invalid(self):
        """Test http_token with invalid tokens."""
        validator = Validator.http_token()

        invalid_tokens = [
            "",  # Empty
            "token with space",  # Space not allowed
            "token\ttab",  # Tab not allowed
            "token\nnewline",  # Newline not allowed
            "token(paren)",  # Parentheses not allowed
            "token[bracket]",  # Brackets not allowed
            "token{brace}",  # Braces not allowed
            "token<angle>",  # Angle brackets not allowed
            "token@at",  # @ not allowed in http_token
            "token,comma",  # Comma not allowed
            "token;semicolon",  # Semicolon not allowed
            "token:colon",  # Colon not allowed
            "token=equals",  # Equals not allowed
            "token?question",  # Question mark not allowed
            "token/slash",  # Forward slash not allowed
            "token\\backslash",  # Backslash not allowed
            "token\"quote\"",  # Quotes not allowed
        ]

        for token in invalid_tokens:
            with pytest.raises(SymbolResolutionError):
                validator(token)

    def test_regex_validator_factory(self):
        """Test the unified regex_validator factory method."""
        import re

        # Create a custom validator for testing
        test_pattern = re.compile(r'^[A-Z]+$')  # Only uppercase letters
        validator = Validator.regex_validator(test_pattern, "Must be uppercase letters only")

        # Valid cases
        validator("ABC")
        validator("HELLO")
        validator("TEST")

        # Invalid cases
        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("abc")

        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("Hello")

        with pytest.raises(SymbolResolutionError, match="Must be uppercase letters only"):
            validator("TEST123")


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
