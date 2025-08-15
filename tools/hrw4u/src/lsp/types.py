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
TypedDict definitions for LSP structures.

This module provides strongly-typed data structures for the HRW4U LSP server.
"""

from __future__ import annotations

from typing import Any, TypedDict


class LSPPosition(TypedDict):
    """LSP Position structure."""
    line: int
    character: int


class LSPRange(TypedDict):
    """LSP Range structure."""
    start: LSPPosition
    end: LSPPosition


class CompletionContext(TypedDict):
    """Strongly-typed completion context structure."""
    is_section_context: bool
    has_dot: bool
    dot_prefix: str
    is_function_context: bool
    allows_keywords: bool
    current_section: Any | None  # SectionType | None - avoiding circular import
    replacement_range: LSPRange | None


class VariableDeclaration(TypedDict):
    """Variable declaration structure."""
    type: str
    description: str
    line: int


class DiagnosticRange(TypedDict):
    """Diagnostic range structure."""
    start: LSPPosition
    end: LSPPosition


class LSPDiagnostic(TypedDict):
    """LSP Diagnostic structure."""
    range: DiagnosticRange
    severity: int
    message: str
    source: str


class LSPCompletionItem(TypedDict, total=False):
    """LSP Completion Item structure."""
    label: str
    kind: int
    detail: str
    documentation: dict[str, str]
    insertText: str
    textEdit: dict[str, Any]
    insertTextFormat: int


class LSPHoverInfo(TypedDict):
    """LSP Hover information structure."""
    contents: dict[str, str]


class LSPTextDocument(TypedDict):
    """LSP Text Document structure."""
    uri: str
    languageId: str
    version: int
    text: str


class LSPMessage(TypedDict):
    """Base LSP message structure."""
    jsonrpc: str
    id: int | str | None


class LSPRequest(LSPMessage):
    """LSP Request structure."""
    method: str
    params: dict[str, Any]


class LSPResponse(LSPMessage):
    """LSP Response structure."""
    result: Any | None
    error: dict[str, Any] | None


class LSPNotification(TypedDict):
    """LSP Notification structure."""
    jsonrpc: str
    method: str
    params: dict[str, Any]
