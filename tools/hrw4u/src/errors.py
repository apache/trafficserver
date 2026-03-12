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

import re
from dataclasses import dataclass
from typing import Final

from antlr4.error.ErrorListener import ErrorListener

_TOKEN_NAMES: Final[dict[str, str]] = {
    'QUALIFIED_IDENT': "qualified name (e.g. 'Namespace::Name')",
    'IDENT': 'identifier',
    'LPAREN': "'('",
    'RPAREN': "')'",
    'LBRACE': "'{'",
    'RBRACE': "'}'",
    'LBRACKET': "'['",
    'RBRACKET': "']'",
    'SEMICOLON': "';'",
    'COLON': "':'",
    'COMMA': "','",
    'EQUAL': "'='",
    'EQUALS': "'=='",
    'PLUSEQUAL': "'+='",
    'NEQ': "'!='",
    'DOLLAR': "'$'",
    'STRING': 'string literal',
    'NUMBER': 'number',
    'REGEX': 'regex pattern',
    'IPV4_LITERAL': 'IPv4 address',
    'IPV6_LITERAL': 'IPv6 address',
    'COMMENT': 'comment',
    'AND': "'&&'",
    'OR': "'||'",
    'TILDE': "'~'",
    'NOT_TILDE': "'!~'",
    'GT': "'>'",
    'LT': "'<'",
    'AT': "'@'",
}

_TOKEN_PATTERN: Final = re.compile(r'\b(' + '|'.join(re.escape(k) for k in sorted(_TOKEN_NAMES, key=len, reverse=True)) + r')\b')


def humanize_error_message(msg: str) -> str:
    return _TOKEN_PATTERN.sub(lambda m: _TOKEN_NAMES[m.group(1)], msg)


def _format_diagnostic(filename: str, line: int, col: int, severity: str, message: str, source_line: str) -> str:
    header = f"{filename}:{line}:{col}: {severity}: {message}"

    lineno = f"{line:4d}"
    code_line = f"{lineno} | {source_line}"
    pointer_line = f"{' ' * 4} | {' ' * col}^"
    return f"{header}\n{code_line}\n{pointer_line}"


def _extract_source_context(ctx: object) -> tuple[int, int, str]:
    try:
        input_stream = ctx.start.getInputStream()
        source_line = input_stream.strdata.splitlines()[ctx.start.line - 1]
        return ctx.start.line, ctx.start.column, source_line
    except Exception:
        return 0, 0, ""


class ThrowingErrorListener(ErrorListener):

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

        raise Hrw4uSyntaxError(self.filename, line, column, humanize_error_message(msg), code_line)


class Hrw4uSyntaxError(Exception):

    def __init__(self, filename: str, line: int, column: int, message: str, source_line: str) -> None:
        super().__init__(_format_diagnostic(filename, line, column, "error", message, source_line))
        self.filename = filename
        self.line = line
        self.column = column
        self.source_line = source_line


class SymbolResolutionError(Exception):

    def __init__(self, name: str, message: str | None = None) -> None:
        self.name = name
        super().__init__(message or f"Unrecognized symbol: '{name}'")

    def add_symbol_suggestion(self, suggestions: list[str]) -> None:
        if suggestions:
            self.add_note(f"     | Did you mean: {suggestions[0]}?")


def hrw4u_error(filename: str, ctx: object, exc: Exception) -> Hrw4uSyntaxError:
    if isinstance(exc, Hrw4uSyntaxError):
        return exc

    line, col, source_line = _extract_source_context(ctx) if ctx else (0, 0, "")
    error = Hrw4uSyntaxError(filename, line, col, str(exc), source_line)

    if hasattr(exc, '__notes__') and exc.__notes__:
        for note in exc.__notes__:
            error.add_note(note)

    return error


def format_diagnostic(filename: str, ctx: object, severity: str, message: str) -> str:
    """Format a diagnostic message (error/warning) with source context from a parser ctx."""
    line, col, source_line = _extract_source_context(ctx)
    return _format_diagnostic(filename, line, col, severity, message, source_line)


@dataclass(frozen=True, slots=True)
class Warning:
    """Structured warning with source location for use by both CLI and LSP."""
    filename: str
    line: int
    column: int
    message: str
    source_line: str

    def format(self) -> str:
        return _format_diagnostic(self.filename, self.line, self.column, "warning", self.message, self.source_line)

    @classmethod
    def from_ctx(cls, filename: str, ctx: object, message: str) -> Warning:
        line, col, source_line = _extract_source_context(ctx)
        return cls(filename=filename, line=line, column=col, message=message, source_line=source_line)


class ErrorCollector:
    """Collects multiple syntax errors and warnings for comprehensive reporting."""

    def __init__(self, max_errors: int = 5) -> None:
        self.errors: list[Hrw4uSyntaxError] = []
        self.max_errors = max_errors
        self.warnings: list[Warning] = []
        self._sandbox_message: str | None = None

    def add_error(self, error: Hrw4uSyntaxError) -> None:
        self.errors.append(error)

    def add_warning(self, warning: Warning) -> None:
        self.warnings.append(warning)

    def set_sandbox_message(self, message: str) -> None:
        """Record the sandbox policy message to display once at the end."""
        if message and self._sandbox_message is None:
            self._sandbox_message = message

    def has_errors(self) -> bool:
        return bool(self.errors)

    @property
    def at_limit(self) -> bool:
        return len(self.errors) >= self.max_errors

    def has_warnings(self) -> bool:
        return bool(self.warnings)

    def get_error_summary(self) -> str:
        if not self.errors and not self.warnings:
            return "No errors found."

        lines: list[str] = []

        if self.errors:
            count = len(self.errors)
            lines.append(f"Found {count} error{'s' if count > 1 else ''}:")

            for error in self.errors:
                lines.append(str(error))
                if hasattr(error, '__notes__') and error.__notes__:
                    lines.extend(error.__notes__)

        if self.warnings:
            if self.errors:
                lines.append("")
            count = len(self.warnings)
            lines.append(f"{count} warning{'s' if count > 1 else ''}:")
            lines.extend(w.format() for w in self.warnings)

        if self.at_limit:
            lines.append(f"(stopped after {self.max_errors} errors)")

        if self._sandbox_message:
            lines.append("")
            lines.append(self._sandbox_message)

        return "\n".join(lines)


class CollectingErrorListener(ErrorListener):

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

        error = Hrw4uSyntaxError(self.filename, line, column, humanize_error_message(msg), code_line)
        self.error_collector.add_error(error)

        if self.error_collector.at_limit:
            raise error
