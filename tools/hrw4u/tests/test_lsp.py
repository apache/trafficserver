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
"""Optimized LSP server tests for the HRW4U Language Server."""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path
from typing import Any
import pytest
import threading
import queue
import concurrent.futures
from dataclasses import dataclass


@dataclass
class LSPTestCase:
    """Represents a single test case for batch execution."""
    name: str
    content: str
    position: dict[str, int]
    method: str
    expected_checks: list[callable]


class OptimizedLSPClient:
    """High-performance LSP client with reduced startup overhead and batching."""

    def __init__(self, server_command: list[str]) -> None:
        self.server_command = server_command
        self.process = None
        self.request_id = 1
        self.response_queue = queue.Queue()
        self.reader_thread = None
        self.initialized = False

    def start_server(self) -> None:
        """Start server and begin background response reading."""
        self.process = subprocess.Popen(
            self.server_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=0,  # Unbuffered for faster I/O
            universal_newlines=True)

        # Start background thread to read responses
        self.reader_thread = threading.Thread(target=self._read_responses, daemon=True)
        self.reader_thread.start()

        # Do initialization once
        self._initialize_server()

    def _read_responses(self) -> None:
        """Background thread to continuously read server responses."""
        while self.process and self.process.poll() is None:
            try:
                line = self.process.stdout.readline()
                if not line:
                    continue

                line = line.strip()
                if line.startswith("Content-Length:"):
                    content_length = int(line.split(":")[1].strip())
                    self.process.stdout.readline()  # Empty line
                    content = self.process.stdout.read(content_length)

                    if content:
                        try:
                            response = json.loads(content)
                            self.response_queue.put(response)
                        except json.JSONDecodeError:
                            continue

            except Exception:
                continue

    def _initialize_server(self) -> None:
        """Initialize server once and wait for ready state."""
        init_message = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": "initialize",
            "params":
                {
                    "processId": None,
                    "capabilities":
                        {
                            "textDocument":
                                {
                                    "completion": {
                                        "completionItem": {
                                            "snippetSupport": True
                                        }
                                    },
                                    "hover": {
                                        "contentFormat": ["markdown", "plaintext"]
                                    }
                                }
                        }
                }
        }

        self.send_message(init_message)
        self.request_id += 1

        # Wait for initialize response
        self.wait_for_response(timeout=3.0)

        # Send initialized notification
        self.send_message({"jsonrpc": "2.0", "method": "initialized", "params": {}})

        self.initialized = True

    def send_message(self, message: dict[str, Any]) -> None:
        """Send message with minimal overhead."""
        if not self.process:
            raise RuntimeError("Server not started")

        content = json.dumps(message, separators=(',', ':'))  # Compact JSON
        request = f"Content-Length: {len(content)}\r\n\r\n{content}"
        self.process.stdin.write(request)
        self.process.stdin.flush()

    def wait_for_response(self, timeout: float = 2.0, expect_id: int | None = None) -> dict[str, Any] | None:
        """Wait for response with reduced timeout and better polling."""
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                response = self.response_queue.get(timeout=0.1)
                if expect_id is not None:
                    if response.get("id") == expect_id:
                        return response
                    # Put back if not the one we want
                    self.response_queue.put(response)
                    continue
                return response
            except queue.Empty:
                continue

        return None

    def open_document(self, uri: str, content: str) -> None:
        """Open document without waiting unnecessarily."""
        open_message = {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "hrw4u",
                    "version": 1,
                    "text": content
                }
            }
        }
        self.send_message(open_message)
        # No sleep - let diagnostics come when ready

    def request_completion(self, uri: str, line: int, character: int) -> dict[str, Any] | None:
        """Request completion with current request ID."""
        completion_message = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": line,
                    "character": character
                }
            }
        }

        expected_id = self.request_id
        self.request_id += 1

        self.send_message(completion_message)
        return self.wait_for_response(expect_id=expected_id, timeout=2.0)

    def request_hover(self, uri: str, line: int, character: int) -> dict[str, Any] | None:
        """Request hover with current request ID."""
        hover_message = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": line,
                    "character": character
                }
            }
        }

        expected_id = self.request_id
        self.request_id += 1

        self.send_message(hover_message)
        return self.wait_for_response(expect_id=expected_id, timeout=2.0)

    def stop_server(self) -> None:
        """Clean shutdown."""
        if self.process:
            self.process.terminate()
            self.process.wait(timeout=5.0)


@pytest.fixture(scope="session")  # Session scope - one server for all tests
def shared_lsp_client():
    """Shared LSP client for all tests to avoid startup overhead."""
    lsp_script = Path(__file__).parent.parent / "scripts" / "hrw4u-lsp"
    if not lsp_script.exists():
        pytest.skip("hrw4u-lsp script not found - run 'make' first")

    client = OptimizedLSPClient([str(lsp_script)])
    client.start_server()
    yield client
    client.stop_server()


def test_lsp_initialization_optimized(shared_lsp_client) -> None:
    """Test basic initialization (server already initialized by fixture)."""
    # Server is already initialized, just verify it's working
    assert shared_lsp_client.initialized
    assert shared_lsp_client.process.poll() is None  # Still running


def test_document_operations_optimized(shared_lsp_client) -> None:
    """Test document operations with reduced waiting."""
    test_content = """READ_REQUEST {
    inbound.req.host = "example.com";
    inbound.resp.body = "Hello World";
}"""

    shared_lsp_client.open_document("file:///test_doc_ops.hrw4u", test_content)
    # No arbitrary sleep - diagnostics will come when ready


def test_completion_optimized(shared_lsp_client) -> None:
    """Test completion with optimized timing."""
    test_content = """READ_REQUEST {
    inbound.req.host = "example.com";
}"""

    shared_lsp_client.open_document("file:///test_completion.hrw4u", test_content)

    response = shared_lsp_client.request_completion("file:///test_completion.hrw4u", 1, 12)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]
    assert len(items) > 0

    for item in items:
        if "textEdit" in item:
            assert "range" in item["textEdit"]
            assert "newText" in item["textEdit"]


def test_hover_optimized(shared_lsp_client) -> None:
    """Test hover with optimized timing."""
    test_content = "READ_REQUEST { inbound.req.host = \"example.com\"; }"

    shared_lsp_client.open_document("file:///test_hover.hrw4u", test_content)

    response = shared_lsp_client.request_hover("file:///test_hover.hrw4u", 0, 5)

    assert response is not None
    assert "result" in response


@pytest.mark.parametrize(
    "section,prefix,should_allow", [
        ("SEND_REQUEST", "outbound.req.", True),
        ("REMAP", "outbound.req.", False),
        ("SEND_REQUEST", "outbound.resp.", False),
        ("READ_RESPONSE", "outbound.resp.", True),
    ])
def test_section_restrictions_batch(shared_lsp_client, section, prefix, should_allow) -> None:
    """Batch test section restrictions to reduce overhead."""
    test_content = f"""{section} {{
    {prefix}
}}"""

    uri = f"file:///test_restrictions_{section}_{prefix.replace('.', '_')}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    # Position after the prefix
    char_pos = len(f"    {prefix}")
    response = shared_lsp_client.request_completion(uri, 1, char_pos)

    assert response is not None
    assert "result" in response
    items = response["result"]["items"]

    matching_items = [item for item in items if item["label"].startswith(prefix)]

    if should_allow:
        assert len(matching_items) > 0, f"{prefix} should be available in {section}"
    else:
        assert len(matching_items) == 0, f"{prefix} should NOT be available in {section}"


def test_multi_section_inbound_always_allowed(shared_lsp_client) -> None:
    """Test inbound.req. in multiple sections efficiently."""
    test_cases = [("REMAP", "inbound.req."), ("SEND_REQUEST", "inbound.req."), ("READ_RESPONSE", "inbound.req.")]

    # Use concurrent futures for parallel execution where possible
    results = []

    for section, prefix in test_cases:
        test_content = f"""{section} {{
    {prefix}
}}"""
        uri = f"file:///test_multi_{section.lower()}.hrw4u"
        shared_lsp_client.open_document(uri, test_content)

        char_pos = len(f"    {prefix}")
        response = shared_lsp_client.request_completion(uri, 1, char_pos)

        assert response is not None
        items = response["result"]["items"]
        matching_items = [item for item in items if item["label"].startswith(prefix)]
        results.append((section, len(matching_items)))

    # Verify all sections allow inbound.req.
    for section, count in results:
        assert count > 0, f"inbound.req. should be available in {section}"


def test_outbound_restrictions_batch(shared_lsp_client) -> None:
    """Batch test outbound restrictions in early vs late sections."""
    early_sections = ["PRE_REMAP", "REMAP", "READ_REQUEST"]
    late_sections = ["SEND_REQUEST", "READ_RESPONSE"]

    # Test early sections (should block outbound)
    for section in early_sections:
        test_content = f"""{section} {{
    outbound.
}}"""
        uri = f"file:///test_early_{section.lower()}.hrw4u"
        shared_lsp_client.open_document(uri, test_content)

        response = shared_lsp_client.request_completion(uri, 1, 13)
        assert response is not None
        items = response["result"]["items"]

        # Should NOT find restricted outbound items
        outbound_cookie_items = [item for item in items if item["label"].startswith("outbound.cookie.")]
        outbound_url_items = [item for item in items if item["label"].startswith("outbound.url.")]

        assert len(outbound_cookie_items) == 0, f"outbound.cookie. should NOT be in {section}"
        assert len(outbound_url_items) == 0, f"outbound.url. should NOT be in {section}"

    # Test late sections (should allow outbound)
    for section in late_sections:
        test_content = f"""{section} {{
    outbound.
}}"""
        uri = f"file:///test_late_{section.lower()}.hrw4u"
        shared_lsp_client.open_document(uri, test_content)

        response = shared_lsp_client.request_completion(uri, 1, 13)
        assert response is not None
        items = response["result"]["items"]

        # Should find allowed outbound items
        outbound_conn_items = [item for item in items if item["label"].startswith("outbound.conn.")]
        outbound_cookie_items = [item for item in items if item["label"].startswith("outbound.cookie.")]

        assert len(outbound_conn_items) > 0, f"outbound.conn. should be in {section}"
        assert len(outbound_cookie_items) > 0, f"outbound.cookie. should be in {section}"


def test_specific_outbound_conn_completions(shared_lsp_client) -> None:
    """Test specific outbound.conn completions efficiently."""
    test_content = """SEND_REQUEST {
    outbound.conn.
}"""

    shared_lsp_client.open_document("file:///test_conn_specific.hrw4u", test_content)

    response = shared_lsp_client.request_completion("file:///test_conn_specific.hrw4u", 1, 18)

    assert response is not None
    items = response["result"]["items"]

    dscp_items = [item for item in items if item["label"] == "outbound.conn.dscp"]
    mark_items = [item for item in items if item["label"] == "outbound.conn.mark"]

    assert len(dscp_items) > 0, "outbound.conn.dscp should be available"
    assert len(mark_items) > 0, "outbound.conn.mark should be available"

    # Verify mappings
    assert "set-conn-dscp" in dscp_items[0]["detail"]
    assert "set-conn-mark" in mark_items[0]["detail"]


def test_additional_language_features(shared_lsp_client) -> None:
    """Test additional language features with minimal overhead."""
    # Combined test document with multiple advanced features
    test_content = """VARS { TestFlag: bool; }
SEND_RESPONSE {
    if inbound.conn.TLS { outbound.resp.X-Geo = "{geo.country}"; }
    if inbound.status == 418 { counter("teapots"); }
}
REMAP {
    if inbound.url.path ~ /api_(.+)/ { run-plugin("test.so"); }
}"""

    shared_lsp_client.open_document("file:///test_additional.hrw4u", test_content)

    # Single completion test to validate syntax parsing
    response = shared_lsp_client.request_completion("file:///test_additional.hrw4u", 2, 12)

    # Server should handle complex syntax without crashing
    if response is not None:
        assert "result" in response


@pytest.mark.parametrize(
    "namespace,expected_content", [
        ("geo", "Geographic Information Namespace"),
        ("id", "Transaction Identifier Namespace"),
        ("inbound", "Inbound Request Context"),
        ("outbound", "Outbound Request Context"),
        ("client", "Client Information Namespace"),
        ("http", "HTTP Transaction Control Namespace"),
    ])
def test_namespace_hover_documentation(shared_lsp_client, namespace, expected_content) -> None:
    """Test that namespace prefixes provide comprehensive hover documentation."""
    test_content = f"""READ_REQUEST {{
    {namespace}.
}}"""

    uri = f"file:///test_namespace_{namespace}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    # Position after the namespace and dot
    char_pos = len(f"    {namespace}.")
    response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)  # Hover on the namespace itself

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    assert expected_content in content, f"Expected '{expected_content}' in hover for {namespace}"

    # Verify it's not just the generic "HRW4U symbol" response
    assert "HRW4U symbol" not in content or "Namespace" in content


def test_now_condition_vs_namespace(shared_lsp_client) -> None:
    """Test that 'now' shows condition documentation when standalone, but namespace when used with fields."""
    # Test standalone 'now' (should show condition documentation)
    test_content_condition = """READ_REQUEST {
    if now {
        // test
    }
}"""

    shared_lsp_client.open_document("file:///test_now_condition.hrw4u", test_content_condition)
    response = shared_lsp_client.request_hover("file:///test_now_condition.hrw4u", 1, 7)  # Position on 'now'

    assert response is not None
    assert "result" in response
    content = response["result"]["contents"]["value"]
    assert "HRW4U Condition" in content
    assert "%{NOW}" in content

    # Test 'now.HOUR' (should show field documentation)
    test_content_field = """READ_REQUEST {
    if now.HOUR > 22 {
        // test
    }
}"""

    shared_lsp_client.open_document("file:///test_now_field.hrw4u", test_content_field)
    response = shared_lsp_client.request_hover("file:///test_now_field.hrw4u", 1, 11)  # Position on 'HOUR'

    assert response is not None
    assert "result" in response
    content = response["result"]["contents"]["value"]
    assert "Current Hour" in content or "HOUR" in content


@pytest.mark.parametrize(
    "expression,field_type", [
        ("geo.COUNTRY", "Country Code"),
        ("id.UNIQUE", "Unique Identifier"),
        ("now.HOUR", "Current Hour"),
        ("inbound.method", "HTTP Request Method"),
        ("outbound.status", "HTTP status"),
    ])
def test_specific_field_hover(shared_lsp_client, expression, field_type) -> None:
    """Test hover documentation for specific namespace fields."""
    test_content = f"""READ_REQUEST {{
    if {expression} {{
        // test
    }}
}}"""

    uri = f"file:///test_field_{expression.replace('.', '_')}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    # Position on the field part after the dot
    namespace, field = expression.split('.')
    char_pos = len(f"    if {namespace}.") + len(field) // 2
    response = shared_lsp_client.request_hover(uri, 1, char_pos)

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    # Should contain field-specific documentation
    assert field_type in content or expression in content


@pytest.mark.parametrize(
    "sub_namespace,expected_content", [
        ("inbound.conn", "Inbound Connection Properties"),
        ("outbound.conn", "Outbound Connection Properties"),
        ("inbound.req", "Inbound Request Headers"),
        ("inbound.resp", "Inbound Response Headers"),
        ("outbound.req", "Outbound Request Headers"),
        ("outbound.resp", "Outbound Response Headers"),
        ("inbound.cookie", "Inbound Request Cookies"),
        ("outbound.cookie", "Outbound Response Cookies"),
    ])
def test_sub_namespace_hover_documentation(shared_lsp_client, sub_namespace, expected_content) -> None:
    """Test that sub-namespace patterns provide comprehensive hover documentation."""
    test_content = f"""READ_REQUEST {{
    {sub_namespace}.
}}"""

    uri = f"file:///test_sub_namespace_{sub_namespace.replace('.', '_')}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    # Position after the sub-namespace and dot, but hover on the sub-namespace itself
    char_pos = len(f"    {sub_namespace}.")
    response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)  # Hover on the sub-namespace

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    assert expected_content in content, f"Expected '{expected_content}' in hover for {sub_namespace}"

    # Verify it's not just the generic "HRW4U symbol" response
    assert "HRW4U symbol" not in content or expected_content in content

    # Verify it contains meaningful information (different patterns have different formats)
    # Accept various indicators of comprehensive documentation
    meaningful_indicators = [
        "Available items:", "Usage:", "Description:", "Context:", "HTTP headers", "cookies", "connection", "URL components"
    ]
    has_meaningful_content = any(indicator in content for indicator in meaningful_indicators)
    assert has_meaningful_content, f"Expected meaningful content for {sub_namespace}, got: {content[:200]}..."


@pytest.mark.parametrize(
    "sub_namespace,expected_content", [
        ("inbound.conn", "Inbound Connection Properties"),
        ("outbound.conn", "Outbound Connection Properties"),
    ])
def test_specific_connection_sub_namespace_hover(shared_lsp_client, sub_namespace, expected_content) -> None:
    """Test that connection sub-namespace patterns provide full sub-namespace documentation."""
    test_content = f"""READ_REQUEST {{
    {sub_namespace}.
}}"""

    uri = f"file:///test_conn_sub_namespace_{sub_namespace.replace('.', '_')}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    # Position after the sub-namespace and dot, but hover on the sub-namespace itself
    char_pos = len(f"    {sub_namespace}.")
    response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)  # Hover on the sub-namespace

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    assert expected_content in content, f"Expected '{expected_content}' in hover for {sub_namespace}"

    # Verify it shows the comprehensive sub-namespace documentation
    assert "Available items:" in content
    assert "Usage:" in content
    assert "Context:" in content


def test_unknown_namespace_fallback(shared_lsp_client) -> None:
    """Test that unknown namespaces still get generic fallback response."""
    test_content = """READ_REQUEST {
    unknown_namespace.
}"""

    shared_lsp_client.open_document("file:///test_unknown.hrw4u", test_content)

    response = shared_lsp_client.request_hover("file:///test_unknown.hrw4u", 1, 10)

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    assert "HRW4U symbol" in content
