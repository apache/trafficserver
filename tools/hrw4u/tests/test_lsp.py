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
"""LSP server tests for the HRW4U Language Server."""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path
from typing import Any
import pytest
import threading
import queue
from dataclasses import dataclass
from collections import deque
import os
import lsp_asserts


@dataclass
class LSPTestCase:
    """Represents a single test case for batch execution."""
    name: str
    content: str
    position: dict[str, int]
    method: str
    expected_checks: list[callable]


class LSPClient:
    """LSP client for tests"""

    def __init__(self, server_command: list[str]) -> None:
        self.server_command = server_command
        self.process = None
        self.request_id = 1
        self.response_queue = queue.Queue()
        self.reader_thread = None
        self.stderr_thread = None
        self.stderr_buffer = deque(maxlen=1000)
        self.initialized = False
        self.shutdown_requested = False

    def start_server(self) -> None:
        """Start server and begin background response reading."""
        self.process = subprocess.Popen(
            self.server_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,  # Binary mode for proper LSP framing
            bufsize=0)

        # Start background threads for stdout and stderr
        self.reader_thread = threading.Thread(target=self._read_responses, daemon=True)
        self.reader_thread.start()

        self.stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self.stderr_thread.start()

        self._initialize_server()

    def _read_responses(self) -> None:
        """Background thread to continuously read server responses with proper LSP framing."""
        while self.process and self.process.poll() is None and not self.shutdown_requested:
            try:
                # Read headers until blank line
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        return  # EOF

                    line = line.decode('ascii', errors='ignore').strip()
                    if not line:  # Empty line marks end of headers
                        break

                    if ':' in line:
                        key, value = line.split(':', 1)
                        headers[key.strip().lower()] = value.strip()

                # Get content length from headers
                content_length_str = headers.get('content-length')
                if content_length_str is None:
                    continue  # Invalid LSP message - no Content-Length

                try:
                    content_length = int(content_length_str)
                except ValueError:
                    continue  # Invalid Content-Length value

                # Read exact number of bytes for the body
                content_bytes = self.process.stdout.read(content_length)
                if len(content_bytes) != content_length:
                    continue  # Incomplete read

                # Decode as UTF-8 after full payload is read
                try:
                    content = content_bytes.decode('utf-8')
                    response = json.loads(content)
                    self.response_queue.put(response)
                except json.JSONDecodeError as e:
                    # Log specific JSON decode error but continue
                    continue
                except UnicodeDecodeError as e:
                    # Log specific Unicode decode error but continue
                    continue

            except (IOError, OSError, BrokenPipeError):
                # Connection-related errors - break the loop
                break
            except Exception as e:
                # Store unexpected errors for debugging but continue
                continue

    def _drain_stderr(self) -> None:
        """Background thread to continuously drain stderr to prevent deadlocks."""
        while self.process and self.process.poll() is None and not self.shutdown_requested:
            try:
                line = self.process.stderr.readline()
                if not line:
                    break
                line = line.decode('utf-8', errors='ignore').strip()
                if line:
                    self.stderr_buffer.append(line)
            except Exception:
                break

    def get_server_stderr(self, last_n: int = 20) -> list[str]:
        """Get the last N lines of server stderr for debugging."""
        return list(self.stderr_buffer)[-last_n:]

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
        self.wait_for_response(timeout=3.0)
        self.send_message({"jsonrpc": "2.0", "method": "initialized", "params": {}})
        self.initialized = True

    def send_message(self, message: dict[str, Any]) -> None:
        """Send message with proper binary LSP framing."""
        if not self.process:
            raise RuntimeError("Server not started")

        content = json.dumps(message, separators=(',', ':'), ensure_ascii=False)  # Compact JSON
        content_bytes = content.encode('utf-8')
        content_length = len(content_bytes)

        # Build LSP message with proper byte length
        request = f"Content-Length: {content_length}\r\n\r\n".encode('ascii') + content_bytes
        self.process.stdin.write(request)
        self.process.stdin.flush()

    def wait_for_response(self, timeout: float = 2.0, expect_id: int | None = None) -> dict[str, Any] | None:
        """Wait for response with adaptive backoff and configurable timeout."""
        # Allow environment override for timeout
        env_timeout = os.environ.get('HRW4U_LSP_TIMEOUT')
        if env_timeout:
            try:
                timeout = float(env_timeout)
            except ValueError:
                pass  # Use default if invalid

        start_time = time.time()
        poll_interval = 0.1  # Start with fast polling
        max_poll_interval = 0.5  # Cap at 0.5s

        while time.time() - start_time < timeout:
            try:
                response = self.response_queue.get(timeout=poll_interval)
                if expect_id is not None:
                    if response.get("id") == expect_id:
                        return response
                    # Put back if not the one we want
                    self.response_queue.put(response)
                    continue
                return response
            except queue.Empty:
                # Gradually increase polling interval for backoff
                poll_interval = min(poll_interval * 1.2, max_poll_interval)
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
        """Graceful shutdown with JSON-RPC shutdown sequence and fallback kill."""
        if not self.process:
            return

        self.shutdown_requested = True

        try:
            # Send JSON-RPC shutdown request
            shutdown_message = {"jsonrpc": "2.0", "id": self.request_id, "method": "shutdown", "params": {}}

            self.send_message(shutdown_message)

            # Wait for shutdown response (optimized shorter timeout)
            shutdown_response = self.wait_for_response(timeout=0.5, expect_id=self.request_id)
            self.request_id += 1

            # Send exit notification
            exit_message = {"jsonrpc": "2.0", "method": "exit", "params": {}}
            self.send_message(exit_message)

            # Wait for process to exit gracefully (optimized timeout)
            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                # Fallback: terminate if it doesn't exit gracefully
                self.process.terminate()
                try:
                    self.process.wait(timeout=0.5)
                except subprocess.TimeoutExpired:
                    # Last resort: kill
                    self.process.kill()
                    self.process.wait()

        except Exception:
            # If anything goes wrong, fall back to terminate (optimized timeout)
            try:
                self.process.terminate()
                self.process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

        # Ensure threads complete
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=1.0)
        if self.stderr_thread and self.stderr_thread.is_alive():
            self.stderr_thread.join(timeout=1.0)


def _create_test_document(client, content: str, test_name: str) -> str:
    """Helper to create and open a test document."""
    uri = f"file:///test_{test_name}.hrw4u"
    client.open_document(uri, content)
    return uri


def _assert_hover_response(response: dict[str, Any] | None, expected_content: str, context: str) -> str:
    """Helper to validate hover response structure and extract content - use new centralized helpers."""
    return lsp_asserts.assert_hover_contents(response, expected_content, context)


@pytest.fixture(scope="session")
def shared_lsp_client():
    """Shared LSP client for all tests to avoid startup overhead."""
    lsp_script = Path(__file__).parent.parent / "scripts" / "hrw4u-lsp"
    if not lsp_script.exists():
        pytest.skip("hrw4u-lsp script not found - run 'make' first")

    client = LSPClient([str(lsp_script)])
    client.start_server()
    yield client
    client.stop_server()


@pytest.mark.parametrize(
    "section,prefix,should_allow", [
        ("REMAP", "inbound.req.", True),
        ("SEND_REQUEST", "outbound.req.", True),
        ("SEND_REQUEST", "outbound.req.", True),
        ("READ_RESPONSE", "outbound.resp.", True),
    ])
def test_section_restrictions_batch(shared_lsp_client, section, prefix, should_allow) -> None:
    """Batch test section restrictions to reduce overhead."""
    test_content = f"""{section} {{
    {prefix}
}}"""

    uri = f"file:///test_restrictions_{section}_{prefix.replace('.', '_')}.hrw4u"
    shared_lsp_client.open_document(uri, test_content)

    char_pos = len(f"    {prefix}")
    response = shared_lsp_client.request_completion(uri, 1, char_pos)

    items = lsp_asserts.assert_result_items(response, f"completion in {section}")
    lsp_asserts.assert_completion_items_with_prefix(items, prefix, should_allow, f"{section} completion for {prefix}")


def test_multi_section_inbound_always_allowed(shared_lsp_client) -> None:
    """Test inbound.req. in multiple sections efficiently."""
    test_cases = [("REMAP", "inbound.req."), ("SEND_REQUEST", "inbound.req."), ("READ_RESPONSE", "inbound.req.")]

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

    for section, count in results:
        assert count > 0, f"inbound.req. should be available in {section}"


def test_outbound_restrictions_batch(shared_lsp_client) -> None:
    """Batch test outbound restrictions - outbound features have section-specific availability."""
    # outbound.url. is available in PRE_REMAP through SEND_REQUEST, plus READ_RESPONSE, SEND_RESPONSE
    # outbound.cookie. is only available from SEND_REQUEST onwards
    http_sections = ["PRE_REMAP", "REMAP", "READ_REQUEST", "SEND_REQUEST", "READ_RESPONSE"]

    for section in http_sections:
        test_content = f"""{section} {{
    outbound.
}}"""
        uri = f"file:///test_outbound_{section.lower()}.hrw4u"
        shared_lsp_client.open_document(uri, test_content)

        response = shared_lsp_client.request_completion(uri, 1, 13)
        assert response is not None
        items = response["result"]["items"]

        outbound_cookie_items = [item for item in items if item["label"].startswith("outbound.cookie.")]
        outbound_url_items = [item for item in items if item["label"].startswith("outbound.url.")]

        # outbound.cookie. is only available from SEND_REQUEST onwards
        if section in ["SEND_REQUEST", "READ_RESPONSE"]:
            assert len(outbound_cookie_items) > 0, f"outbound.cookie. should be in {section}"
        # outbound.url. is available in all these sections
        assert len(outbound_url_items) > 0, f"outbound.url. should be in {section}"


def test_specific_outbound_conn_completions(shared_lsp_client) -> None:
    """Test specific outbound.conn completions (dscp/mark only available from SEND_REQUEST onwards)"""
    # outbound.conn.dscp and outbound.conn.mark are only available in SEND_REQUEST, READ_RESPONSE, SEND_RESPONSE
    test_content = """SEND_REQUEST {
    outbound.conn.
}"""

    shared_lsp_client.open_document("file:///test_conn_specific.hrw4u", test_content)

    response = shared_lsp_client.request_completion("file:///test_conn_specific.hrw4u", 1, 18)

    items = lsp_asserts.assert_result_items(response, "outbound.conn completions")

    dscp_item = lsp_asserts.assert_completion_item_exists(items, "outbound.conn.dscp", "dscp completion")
    mark_item = lsp_asserts.assert_completion_item_exists(items, "outbound.conn.mark", "mark completion")

    assert "set-conn-dscp" in dscp_item["detail"]
    assert "set-conn-mark" in mark_item["detail"]


def test_additional_language_features(shared_lsp_client) -> None:
    """Test additional language features with minimal overhead."""
    test_content = """VARS { TestFlag: bool; }
SEND_RESPONSE {
    if inbound.conn.TLS { outbound.resp.X-Geo = "{geo.country}"; }
    if inbound.status == 418 { counter("teapots"); }
}
REMAP {
    if inbound.url.path ~ /api_(.+)/ { run-plugin("test.so"); }
}"""

    shared_lsp_client.open_document("file:///test_additional.hrw4u", test_content)

    response = shared_lsp_client.request_completion("file:///test_additional.hrw4u", 2, 12)
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

    uri = _create_test_document(shared_lsp_client, test_content, f"namespace_{namespace}")

    char_pos = len(f"    {namespace}.")
    response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)

    content = _assert_hover_response(response, expected_content, namespace)
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
    response = shared_lsp_client.request_hover("file:///test_now_condition.hrw4u", 1, 7)

    assert response is not None
    assert "result" in response
    content = response["result"]["contents"]["value"]
    assert "HRW4U Condition" in content
    assert "%{NOW}" in content

    test_content_field = """READ_REQUEST {
    if now.HOUR > 22 {
        // test
    }
}"""

    shared_lsp_client.open_document("file:///test_now_field.hrw4u", test_content_field)
    response = shared_lsp_client.request_hover("file:///test_now_field.hrw4u", 1, 11)

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

    namespace, field = expression.split('.')
    char_pos = len(f"    if {namespace}.") + len(field) // 2
    response = shared_lsp_client.request_hover(uri, 1, char_pos)

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
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
    char_pos = len(f"    {sub_namespace}.")
    response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)  # Hover on the sub-namespace

    assert response is not None
    assert "result" in response
    assert "contents" in response["result"]

    content = response["result"]["contents"]["value"]
    assert expected_content in content, f"Expected '{expected_content}' in hover for {sub_namespace}"
    assert "HRW4U symbol" not in content or expected_content in content

    meaningful_indicators = [
        "Available items:", "Usage:", "Description:", "Context:", "HTTP headers", "cookies", "connection", "URL components"
    ]
    has_meaningful_content = any(indicator in content for indicator in meaningful_indicators)
    assert has_meaningful_content, f"Expected meaningful content for {sub_namespace}, got: {content[:200]}..."


def test_connection_namespace_hover_details(shared_lsp_client) -> None:
    """Test that connection sub-namespaces provide comprehensive documentation details."""
    connection_namespaces = [
        ("inbound.conn", "Inbound Connection Properties"),
        ("outbound.conn", "Outbound Connection Properties"),
    ]

    for sub_namespace, expected_content in connection_namespaces:
        test_content = f"""READ_REQUEST {{
    {sub_namespace}.
}}"""

        uri = _create_test_document(shared_lsp_client, test_content, f"conn_details_{sub_namespace.replace('.', '_')}")

        char_pos = len(f"    {sub_namespace}.")
        response = shared_lsp_client.request_hover(uri, 1, char_pos - 1)

        content = _assert_hover_response(response, expected_content, sub_namespace)

        # Additional checks specific to connection namespaces
        assert "Available items:" in content, f"Missing 'Available items:' for {sub_namespace}"
        assert "Usage:" in content, f"Missing 'Usage:' for {sub_namespace}"
        assert "Context:" in content, f"Missing 'Context:' for {sub_namespace}"


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
