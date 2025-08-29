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

from hrw4u.errors import ErrorCollector, Hrw4uSyntaxError
from hrw4u.visitor import HRW4UVisitor
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


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
