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
"""Error/warning output formatters for hrw4u and u4wrh.

Columns are 0-based across every format (matching the internal representation
used by the ANTLR-driven listeners). The JSON schema is versioned so downstream
consumers (UIs, CI tools) can guard against future changes.
"""

from __future__ import annotations

import json
from abc import ABC, abstractmethod
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from hrw4u.errors import Hrw4uSyntaxError, Warning

JSON_SCHEMA_VERSION = 1


class ErrorFormatter(ABC):
    """Renders a collected batch of errors and warnings into a single string."""

    @abstractmethod
    def format_errors(
            self, errors: list["Hrw4uSyntaxError"], warnings: list["Warning"], sandbox_message: str | None, at_limit: bool,
            max_errors: int) -> str:
        ...


class PlainTextFormatter(ErrorFormatter):
    """Current CLI output: human-readable diagnostics with caret pointers."""

    def format_errors(
            self, errors: list["Hrw4uSyntaxError"], warnings: list["Warning"], sandbox_message: str | None, at_limit: bool,
            max_errors: int) -> str:
        if not errors and not warnings:
            return "No errors found."

        lines: list[str] = []

        if errors:
            count = len(errors)
            if count > 1:
                lines.append(f"Found {count} errors:")

            for error in errors:
                lines.append(str(error))
                notes = getattr(error, '__notes__', None)
                if notes:
                    lines.extend(f"     | {note}" for note in notes)

        if warnings:
            if errors:
                lines.append("")
            count = len(warnings)
            lines.append(f"{count} warning{'s' if count > 1 else ''}:")
            lines.extend(w.format() for w in warnings)

        if at_limit:
            lines.append(f"(stopped after {max_errors} errors)")

        if sandbox_message:
            lines.append("")
            lines.append(sandbox_message)

        return "\n".join(lines)


class JSONFormatter(ErrorFormatter):
    """Machine-readable output. Emits a single compact JSON object per call.

    Suitable for NDJSON pipelines: in bulk mode each input file produces exactly
    one object on one line of stderr.
    """

    def format_errors(
            self, errors: list["Hrw4uSyntaxError"], warnings: list["Warning"], sandbox_message: str | None, at_limit: bool,
            max_errors: int) -> str:
        payload = {
            "version": JSON_SCHEMA_VERSION,
            "errors": [_diag_to_dict(e, "error") for e in errors],
            "warnings": [_diag_to_dict(w, "warning") for w in warnings],
            "summary": {
                "error_count": len(errors),
                "warning_count": len(warnings),
                "truncated": at_limit,
                "max_errors": max_errors
            },
            "sandbox_message": sandbox_message
        }
        return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


class MarkdownFormatter(ErrorFormatter):
    """Markdown report suitable for PR comments, chat, and docs."""

    def format_errors(
            self, errors: list["Hrw4uSyntaxError"], warnings: list["Warning"], sandbox_message: str | None, at_limit: bool,
            max_errors: int) -> str:
        if not errors and not warnings:
            return "_No errors found._"

        parts: list[str] = []
        parts.append(_markdown_heading(len(errors), len(warnings)))

        for error in errors:
            parts.append(
                _markdown_diagnostic(
                    severity="Error",
                    filename=error.filename,
                    line=error.line,
                    column=error.column,
                    message=_extract_plain_message(error),
                    source_line=error.source_line,
                    notes=list(getattr(error, '__notes__', None) or [])))

        for warning in warnings:
            parts.append(
                _markdown_diagnostic(
                    severity="Warning",
                    filename=warning.filename,
                    line=warning.line,
                    column=warning.column,
                    message=warning.message,
                    source_line=warning.source_line,
                    notes=[]))

        if at_limit:
            parts.append(f"> _Stopped after {max_errors} errors._")

        if sandbox_message:
            parts.append(f"> **Sandbox:** {sandbox_message}")

        return "\n\n".join(parts)


def _diag_to_dict(diag: "Hrw4uSyntaxError | Warning", severity: str) -> dict:
    notes = list(getattr(diag, '__notes__', None) or [])
    message = _extract_plain_message(diag)
    return {
        "filename": diag.filename,
        "line": diag.line,
        "column": diag.column,
        "severity": severity,
        "message": message,
        "source_line": diag.source_line,
        "notes": notes
    }


def _extract_plain_message(diag: "Hrw4uSyntaxError | Warning") -> str:
    """Return just the message text, without the file:line:col: prefix or caret art.

    ``Hrw4uSyntaxError`` pre-formats a full diagnostic into ``args[0]``; Warnings
    carry the raw message on ``.message``.
    """
    message = getattr(diag, 'message', None)
    if message is not None:
        return message
    raw = str(diag.args[0]) if diag.args else ""
    header = raw.split("\n", 1)[0]
    prefix = f"{diag.filename}:{diag.line}:{diag.column}: error: "
    if header.startswith(prefix):
        return header[len(prefix):]
    return header


def _markdown_heading(error_count: int, warning_count: int) -> str:
    bits: list[str] = []
    if error_count:
        bits.append(f"{error_count} error{'s' if error_count != 1 else ''}")
    if warning_count:
        bits.append(f"{warning_count} warning{'s' if warning_count != 1 else ''}")
    return f"## hrw4u: {', '.join(bits)}" if bits else "## hrw4u"


def _markdown_diagnostic(
        *, severity: str, filename: str, line: int, column: int, message: str, source_line: str, notes: list[str]) -> str:
    lines = [f"### {severity} — `{filename}:{line}:{column}`", message]

    if source_line:
        pointer = f"{' ' * column}^"
        code_block = f"```\n{line:4d} | {source_line}\n     | {pointer}\n```"
        lines.append(code_block)

    for note in notes:
        lines.append(f"> {note.strip()}")

    return "\n\n".join(lines)


FORMATTERS: dict[str, type[ErrorFormatter]] = {
    "plain": PlainTextFormatter,
    "json": JSONFormatter,
    "markdown": MarkdownFormatter,
}
