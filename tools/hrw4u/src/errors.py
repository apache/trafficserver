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

    def syntaxError(self, recognizer, _offendingSymbol, line: int, column: int, msg: str, e) -> None:
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
    """Formatted syntax error with source context."""

    def __init__(self, filename: str, line: int, column: int, message: str, source_line: str) -> None:
        super().__init__(self._format_error(filename, line, column, message, source_line))

    def _format_error(self, filename: str, line: int, col: int, message: str, source_line: str) -> str:
        error = f"{filename}:{line}:{col}: error: {message}"

        lineno = f"{line:4d}"
        code_line = f"{lineno} | {source_line}"
        pointer_line = f"{' ' * 4} | {' ' * col}^"
        return f"{error}\n{code_line}\n{pointer_line}"


class SymbolResolutionError(Exception):
    """Error for unrecognized symbols during compilation."""

    def __init__(self, name: str, message: str | None = None) -> None:
        self.name = name
        super().__init__(message or f"Unrecognized symbol: '{name}'")


def hrw4u_error(filename: str, ctx, exc: Exception) -> Hrw4uSyntaxError:
    """Convert exceptions to formatted syntax errors with source context."""
    if isinstance(exc, Hrw4uSyntaxError):
        return exc

    try:
        input_stream = ctx.start.getInputStream()
        source_line = input_stream.strdata.splitlines()[ctx.start.line - 1]
    except Exception:
        source_line = ""

    return Hrw4uSyntaxError(filename, ctx.start.line, ctx.start.column, str(exc), source_line)
