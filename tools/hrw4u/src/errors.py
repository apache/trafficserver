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

from antlr4.error.ErrorListener import ErrorListener


class ThrowingErrorListener(ErrorListener):
    """ANTLR error listener that throws exceptions on syntax errors."""

    def __init__(self, filename: str = "<input>") -> None:
        super().__init__()
        self.filename = filename

    def syntaxError(self, recognizer: object, _: object, line: int, column: int, msg: str, e: object) -> None:
        code_line = ""

        try:
            if hasattr(recognizer, 'inputStream'):
                # Lexer
                input_stream = recognizer.inputStream
            else:
                # Parser
                input_stream = recognizer.getInputStream().tokenSource.inputStream
            if input_stream is not None:
                code_line = input_stream.strdata.splitlines()[line - 1]
        except Exception:
            pass

        raise Hrw4uSyntaxError(self.filename, line, column, msg, code_line)


class Hrw4uSyntaxError(Exception):
    """Formatted syntax error with source context and Python 3.11+ exception notes."""

    def __init__(self, filename: str, line: int, column: int, message: str, source_line: str) -> None:
        super().__init__(self._format_error(filename, line, column, message, source_line))
        self.filename = filename
        self.line = line
        self.column = column
        self.source_line = source_line

    def add_context_note(self, context: str) -> None:
        """Add contextual information using Python 3.11+ exception notes."""
        self.add_note(f"Context: {context}")

    def add_resolution_hint(self, hint: str) -> None:
        """Add resolution hint using Python 3.11+ exception notes."""
        self.add_note(f"Hint: {hint}")

    def _format_error(self, filename: str, line: int, col: int, message: str, source_line: str) -> str:
        error = f"{filename}:{line}:{col}: error: {message}"

        lineno = f"{line:4d}"
        code_line = f"{lineno} | {source_line}"
        pointer_line = f"{' ' * 4} | {' ' * col}^"
        return f"{error}\n{code_line}\n{pointer_line}"


class SymbolResolutionError(Exception):
    """Error for unrecognized symbols during compilation with enhanced context."""

    def __init__(self, name: str, message: str | None = None) -> None:
        self.name = name
        super().__init__(message or f"Unrecognized symbol: '{name}'")

    def add_section_context(self, section: str) -> None:
        """Add section context using Python 3.11+ exception notes."""
        self.add_note(f"While processing section: {section}")

    def add_symbol_suggestion(self, suggestions: list[str]) -> None:
        """Add symbol suggestions using Python 3.11+ exception notes."""
        if suggestions:
            self.add_note(f"Did you mean: {', '.join(suggestions[:3])}?")


def hrw4u_error(filename: str, ctx: object, exc: Exception) -> Hrw4uSyntaxError:
    """Convert exceptions to formatted syntax errors with source context."""
    if isinstance(exc, Hrw4uSyntaxError):
        return exc

    try:
        input_stream = ctx.start.getInputStream()
        source_line = input_stream.strdata.splitlines()[ctx.start.line - 1]
    except Exception:
        source_line = ""

    return Hrw4uSyntaxError(filename, ctx.start.line, ctx.start.column, str(exc), source_line)


class ErrorCollector:
    """
    Collects multiple syntax errors during parsing to provide comprehensive error reporting.

    The ErrorCollector implements a tolerant error handling strategy that continues parsing
    after encountering syntax errors, accumulating all errors found in a single pass.
    This provides a better user experience by showing all issues at once rather than
    requiring multiple fix-and-retry cycles.

    The collector works in conjunction with CollectingErrorListener to capture
    ANTLR syntax errors and convert them to structured Hrw4uSyntaxError objects.
    """

    def __init__(self) -> None:
        """Initialize an empty error collector."""
        self.errors: list[Hrw4uSyntaxError] = []

    def add_error(self, error: Hrw4uSyntaxError) -> None:
        """
        Add a syntax error to the collection.
        """
        self.errors.append(error)

    def has_errors(self) -> bool:
        """
        Check if any errors have been collected.
        """
        return bool(self.errors)

    def get_error_summary(self) -> str:
        """
        Generate a formatted summary of all collected errors.
        """
        if not self.errors:
            return "No errors found."

        count = len(self.errors)
        lines = [f"Found {count} error{'s' if count > 1 else ''}:"]
        lines.extend(str(error) for error in self.errors)
        return "\n".join(lines)


class CollectingErrorListener(ErrorListener):
    """
    ANTLR error listener that collects syntax errors instead of throwing exceptions.

    This error listener implements a tolerant parsing strategy by capturing syntax
    errors and adding them to an ErrorCollector rather than immediately failing.
    This allows the parser to continue processing and discover additional errors
    in a single pass.

    The listener extracts source context from the input stream to provide
    helpful error messages with line numbers and source code snippets.
    """

    def __init__(self, filename: str = "<input>", error_collector: ErrorCollector | None = None) -> None:
        """
        Initialize the collecting error listener.

        Args:
            filename: Name of the file being parsed (for error reporting)
            error_collector: Existing collector to use, or None to create a new one
        """
        super().__init__()
        self.filename = filename
        self.error_collector = error_collector or ErrorCollector()

    def syntaxError(self, recognizer: object, _: object, line: int, column: int, msg: str, e: object) -> None:
        """
        Handle a syntax error by collecting it rather than throwing an exception.

        This method is called by ANTLR when a syntax error is encountered.
        It extracts the source line from the input stream and creates a
        structured Hrw4uSyntaxError with helpful formatting.
        """
        code_line = ""

        try:
            if hasattr(recognizer, 'inputStream'):
                # Lexer case - direct access to input stream
                input_stream = recognizer.inputStream
            else:
                # Parser case - access through token source
                input_stream = recognizer.getInputStream().tokenSource.inputStream

            if input_stream is not None:
                # Extract the source line for context
                code_line = input_stream.strdata.splitlines()[line - 1]
        except Exception:
            # Gracefully handle any issues accessing the source
            pass

        error = Hrw4uSyntaxError(self.filename, line, column, msg, code_line)
        self.error_collector.add_error(error)
