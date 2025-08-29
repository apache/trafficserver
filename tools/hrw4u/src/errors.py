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

    def __init__(self, name: str, message: str | None = None) -> None:
        self.name = name
        super().__init__(message or f"Unrecognized symbol: '{name}'")

    def add_symbol_suggestion(self, suggestions: list[str]) -> None:
        if suggestions:
            self.add_note(f"     | Did you mean: {suggestions[0]}?")


def hrw4u_error(filename: str, ctx: object, exc: Exception) -> Hrw4uSyntaxError:
    """Convert exceptions to formatted syntax errors with source context."""
    if isinstance(exc, Hrw4uSyntaxError):
        return exc

    if ctx is None:
        error = Hrw4uSyntaxError(filename, 0, 0, str(exc), "")
    else:
        try:
            input_stream = ctx.start.getInputStream()
            source_line = input_stream.strdata.splitlines()[ctx.start.line - 1]
        except Exception:
            source_line = ""

        error = Hrw4uSyntaxError(filename, ctx.start.line, ctx.start.column, str(exc), source_line)

    if hasattr(exc, '__notes__') and exc.__notes__:
        for note in exc.__notes__:
            error.add_note(note)

    return error


class ErrorCollector:
    """Collects multiple syntax errors for comprehensive error reporting."""

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
        if not self.errors:
            return "No errors found."

        count = len(self.errors)
        lines = [f"Found {count} error{'s' if count > 1 else ''}:"]

        for error in self.errors:
            lines.append(str(error))
            if hasattr(error, '__notes__') and error.__notes__:
                lines.extend(error.__notes__)

        return "\n".join(lines)


class CollectingErrorListener(ErrorListener):
    """ANTLR error listener that collects syntax errors for tolerant parsing."""

    def __init__(self, filename: str = "<input>", error_collector: ErrorCollector | None = None) -> None:
        super().__init__()
        self.filename = filename
        self.error_collector = error_collector or ErrorCollector()

    def syntaxError(self, recognizer: object, _: object, line: int, column: int, msg: str, e: object) -> None:
        code_line = ""

        try:
            if hasattr(recognizer, 'inputStream'):
                input_stream = recognizer.inputStream
            else:
                input_stream = recognizer.getInputStream().tokenSource.inputStream

            if input_stream is not None:
                code_line = input_stream.strdata.splitlines()[line - 1]
        except Exception:
            pass

        error = Hrw4uSyntaxError(self.filename, line, column, msg, code_line)
        self.error_collector.add_error(error)
