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


class LSPTestClient:

    def __init__(self, server_command: list[str]) -> None:
        self.server_command = server_command
        self.process = None
        self.request_id = 1

    def start_server(self) -> None:
        self.process = subprocess.Popen(
            self.server_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            universal_newlines=True)

    def send_message(self, message: dict[str, Any]) -> None:
        if not self.process:
            raise RuntimeError("Server not started")

        content = json.dumps(message)
        request = f"Content-Length: {len(content)}\r\n\r\n{content}"
        self.process.stdin.write(request)
        self.process.stdin.flush()

    def read_response(self, timeout: float = 5.0, expect_id: int | None = None) -> dict[str, Any] | None:
        if not self.process:
            return None

        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.process.poll() is not None:
                return None

            try:
                line = self.process.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue

                line = line.strip()
                if line.startswith("Content-Length:"):
                    content_length = int(line.split(":")[1].strip())
                    self.process.stdout.readline()
                    content = self.process.stdout.read(content_length)
                    if not content:
                        time.sleep(0.1)
                        continue

                    try:
                        response = json.loads(content)
                        if expect_id is not None:
                            if response.get("id") == expect_id:
                                return response
                            continue
                        return response
                    except json.JSONDecodeError:
                        continue

            except Exception:
                time.sleep(0.1)
                continue

            time.sleep(0.1)

        return None

    def stop_server(self) -> None:
        if self.process:
            self.process.terminate()
            self.process.wait()

    def _send_init_sequence(self) -> None:
        init_message = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": None,
                "capabilities": {
                    "textDocument": {
                        "completion": {},
                        "hover": {}
                    }
                }
            }
        }
        self.send_message(init_message)
        self.read_response()
        self.send_message({"jsonrpc": "2.0", "method": "initialized", "params": {}})


@pytest.fixture
def lsp_client():
    # Look for the local hrw4u-lsp script
    lsp_script = Path(__file__).parent.parent / "scripts" / "hrw4u-lsp"
    if not lsp_script.exists():
        pytest.skip("hrw4u-lsp script not found - run 'make' first")

    client = LSPTestClient([str(lsp_script)])
    client.start_server()
    time.sleep(0.5)
    yield client
    client.stop_server()


def test_lsp_initialization(lsp_client) -> None:
    message = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params":
            {
                "processId": None,
                "clientInfo": {
                    "name": "test-client",
                    "version": "1.0.0"
                },
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

    lsp_client.send_message(message)
    response = lsp_client.read_response()

    assert response is not None
    assert "result" in response
    assert "capabilities" in response["result"]
    capabilities = response["result"]["capabilities"]
    assert "textDocumentSync" in capabilities
    assert "completionProvider" in capabilities
    assert "hoverProvider" in capabilities


def test_document_operations(lsp_client) -> None:
    lsp_client._send_init_sequence()

    test_content = """READ_REQUEST {
    inbound.req.host = "example.com";
    inbound.resp.body = "Hello World";
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }

    lsp_client.send_message(open_message)
    time.sleep(1)
    diagnostics = lsp_client.read_response(timeout=2)
    if diagnostics:
        assert diagnostics.get("method") == "textDocument/publishDiagnostics"
        assert "params" in diagnostics


def test_completion(lsp_client) -> None:
    lsp_client._send_init_sequence()

    test_content = """READ_REQUEST {
    inbound.req.host = "example.com";
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    completion_message = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 1,
                "character": 12
            }
        }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=2)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]
    assert len(items) > 0

    for item in items:
        if "textEdit" in item:
            assert "range" in item["textEdit"]
            assert "newText" in item["textEdit"]


def test_hover(lsp_client) -> None:
    lsp_client._send_init_sequence()

    test_content = "READ_REQUEST { inbound.req.host = \"example.com\"; }"
    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    hover_message = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 0,
                "character": 5
            }
        }
    }

    lsp_client.send_message(hover_message)
    response = lsp_client.read_response(expect_id=3)

    assert response is not None
    assert "result" in response


def test_section_restrictions_outbound_req_allowed_in_send_request(lsp_client) -> None:
    """Test that outbound.req. appears in completions when in SEND_REQUEST section (should be allowed)."""
    lsp_client._send_init_sequence()

    test_content = """SEND_REQUEST {
    outbound.
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    # Position cursor after "outbound."
    completion_message = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 1,
                "character": 13  # After "outbound."
            }
        }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=4)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]

    # Should find outbound.req. in completions since SEND_REQUEST is not in the restricted sections
    outbound_req_items = [item for item in items if item["label"].startswith("outbound.req.")]
    assert len(outbound_req_items) > 0, "outbound.req. should be available in SEND_REQUEST section"


def test_section_restrictions_outbound_req_blocked_in_remap(lsp_client) -> None:
    """Test that outbound.req. does NOT appear in completions when in REMAP section (should be restricted)."""
    lsp_client._send_init_sequence()

    test_content = """REMAP {
    outbound.
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    # Position cursor after "outbound."
    completion_message = {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 1,
                "character": 13  # After "outbound."
            }
        }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=5)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]

    # Should NOT find outbound.req. in completions since REMAP is in the restricted sections
    outbound_req_items = [item for item in items if item["label"].startswith("outbound.req.")]
    assert len(outbound_req_items) == 0, "outbound.req. should NOT be available in REMAP section"


def test_section_restrictions_outbound_resp_blocked_in_send_request(lsp_client) -> None:
    """Test that outbound.resp. does NOT appear in completions when in SEND_REQUEST section (should be restricted)."""
    lsp_client._send_init_sequence()

    test_content = """SEND_REQUEST {
    outbound.
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    # Position cursor after "outbound."
    completion_message = {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 1,
                "character": 13  # After "outbound."
            }
        }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=6)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]

    # Should NOT find outbound.resp. in completions since SEND_REQUEST is in the restricted sections
    outbound_resp_items = [item for item in items if item["label"].startswith("outbound.resp.")]
    assert len(outbound_resp_items) == 0, "outbound.resp. should NOT be available in SEND_REQUEST section"


def test_section_restrictions_outbound_resp_allowed_in_read_response(lsp_client) -> None:
    """Test that outbound.resp. appears in completions when in READ_RESPONSE section (should be allowed)."""
    lsp_client._send_init_sequence()

    test_content = """READ_RESPONSE {
    outbound.
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u",
                "languageId": "hrw4u",
                "version": 1,
                "text": test_content
            }
        }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    # Position cursor after "outbound."
    completion_message = {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": "file:///test.hrw4u"
            },
            "position": {
                "line": 1,
                "character": 13  # After "outbound."
            }
        }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=7)

    assert response is not None
    assert "result" in response
    assert "items" in response["result"]
    items = response["result"]["items"]

    # Should find outbound.resp. in completions since READ_RESPONSE is NOT in the restricted sections
    outbound_resp_items = [item for item in items if item["label"].startswith("outbound.resp.")]
    assert len(outbound_resp_items) > 0, "outbound.resp. should be available in READ_RESPONSE section"


def test_section_restrictions_inbound_req_always_allowed(lsp_client) -> None:
    """Test that inbound.req. appears in completions in all sections (no restrictions)."""
    lsp_client._send_init_sequence()

    for section in ["REMAP", "SEND_REQUEST", "READ_RESPONSE"]:
        test_content = f"""{section} {{
    inbound.
}}"""

        open_message = {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params":
                {
                    "textDocument":
                        {
                            "uri": f"file:///test_{section.lower()}.hrw4u",
                            "languageId": "hrw4u",
                            "version": 1,
                            "text": test_content
                        }
                }
        }
        lsp_client.send_message(open_message)
        time.sleep(0.5)

        # Position cursor after "inbound."
        completion_message = {
            "jsonrpc": "2.0",
            "id": 8 + hash(section) % 100,  # Unique ID for each section
            "method": "textDocument/completion",
            "params":
                {
                    "textDocument": {
                        "uri": f"file:///test_{section.lower()}.hrw4u"
                    },
                    "position": {
                        "line": 1,
                        "character": 12  # After "inbound."
                    }
                }
        }

        lsp_client.send_message(completion_message)
        response = lsp_client.read_response(expect_id=8 + hash(section) % 100)

        assert response is not None, f"Response should not be None for section {section}"
        assert "result" in response, f"Response should have result for section {section}"
        assert "items" in response["result"], f"Response should have items for section {section}"
        items = response["result"]["items"]

        # Should find inbound.req. in completions since it has no section restrictions (None)
        inbound_req_items = [item for item in items if item["label"].startswith("inbound.req.")]
        assert len(inbound_req_items) > 0, f"inbound.req. should be available in {section} section"


def test_section_restrictions_outbound_blocked_in_early_sections(lsp_client) -> None:
    """Test that outbound.conn., outbound.cookie., and outbound.url. are blocked in PRE_REMAP, REMAP, READ_REQUEST."""
    lsp_client._send_init_sequence()

    # Test all early sections where outbound data doesn't exist yet
    for section in ["PRE_REMAP", "REMAP", "READ_REQUEST"]:
        test_content = f"""{section} {{
    outbound.
}}"""

        open_message = {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params":
                {
                    "textDocument":
                        {
                            "uri": f"file:///test_outbound_{section.lower()}.hrw4u",
                            "languageId": "hrw4u",
                            "version": 1,
                            "text": test_content
                        }
                }
        }
        lsp_client.send_message(open_message)
        time.sleep(0.5)

        # Position cursor after "outbound."
        completion_message = {
            "jsonrpc": "2.0",
            "id": 9 + hash(section) % 100,  # Unique ID for each section
            "method": "textDocument/completion",
            "params":
                {
                    "textDocument": {
                        "uri": f"file:///test_outbound_{section.lower()}.hrw4u"
                    },
                    "position": {
                        "line": 1,
                        "character": 13  # After "outbound."
                    }
                }
        }

        lsp_client.send_message(completion_message)
        response = lsp_client.read_response(expect_id=9 + hash(section) % 100)

        assert response is not None, f"Response should not be None for section {section}"
        assert "result" in response, f"Response should have result for section {section}"
        assert "items" in response["result"], f"Response should have items for section {section}"
        items = response["result"]["items"]

        # Should NOT find these outbound items since they're restricted in early sections
        outbound_cookie_items = [item for item in items if item["label"].startswith("outbound.cookie.")]
        outbound_url_items = [item for item in items if item["label"].startswith("outbound.url.")]
        outbound_conn_dscp_items = [item for item in items if item["label"] == "outbound.conn.dscp"]
        outbound_conn_mark_items = [item for item in items if item["label"] == "outbound.conn.mark"]

        assert len(outbound_cookie_items) == 0, f"outbound.cookie. should NOT be available in {section} section"
        assert len(outbound_url_items) == 0, f"outbound.url. should NOT be available in {section} section"
        assert len(outbound_conn_dscp_items) == 0, f"outbound.conn.dscp should NOT be available in {section} section"
        assert len(outbound_conn_mark_items) == 0, f"outbound.conn.mark should NOT be available in {section} section"

        # Also verify that outbound.conn. prefix itself is restricted
        outbound_conn_prefix_items = [
            item for item in items
            if item["label"].startswith("outbound.conn.") and not item["label"] in ["outbound.conn.dscp", "outbound.conn.mark"]
        ]
        assert len(outbound_conn_prefix_items) == 0, f"outbound.conn. prefix should NOT be available in {section} section"


def test_section_restrictions_outbound_allowed_in_later_sections(lsp_client) -> None:
    """Test that outbound.conn. and outbound.cookie. are allowed in SEND_REQUEST and later sections."""
    lsp_client._send_init_sequence()

    # Test later sections where outbound data exists
    for section in ["SEND_REQUEST", "READ_RESPONSE"]:
        test_content = f"""{section} {{
    outbound.
}}"""

        open_message = {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params":
                {
                    "textDocument":
                        {
                            "uri": f"file:///test_outbound_late_{section.lower()}.hrw4u",
                            "languageId": "hrw4u",
                            "version": 1,
                            "text": test_content
                        }
                }
        }
        lsp_client.send_message(open_message)
        time.sleep(0.5)

        # Position cursor after "outbound."
        completion_message = {
            "jsonrpc": "2.0",
            "id": 10 + hash(section) % 100,  # Unique ID for each section
            "method": "textDocument/completion",
            "params":
                {
                    "textDocument": {
                        "uri": f"file:///test_outbound_late_{section.lower()}.hrw4u"
                    },
                    "position": {
                        "line": 1,
                        "character": 13  # After "outbound."
                    }
                }
        }

        lsp_client.send_message(completion_message)
        response = lsp_client.read_response(expect_id=10 + hash(section) % 100)

        assert response is not None, f"Response should not be None for section {section}"
        assert "result" in response, f"Response should have result for section {section}"
        assert "items" in response["result"], f"Response should have items for section {section}"
        items = response["result"]["items"]

        # Should find these outbound items since they're NOT restricted in later sections
        outbound_conn_items = [item for item in items if item["label"].startswith("outbound.conn.")]
        outbound_cookie_items = [item for item in items if item["label"].startswith("outbound.cookie.")]

        assert len(outbound_conn_items) > 0, f"outbound.conn. should be available in {section} section (allowed everywhere)"
        assert len(outbound_cookie_items) > 0, f"outbound.cookie. should be available in {section} section"

        # Note: outbound.url. behavior depends on section - different restrictions in OPERATOR_MAP vs CONDITION_MAP


def test_outbound_conn_specific_completions(lsp_client) -> None:
    """Test that specific outbound.conn properties (dscp, mark) show up in completions."""
    lsp_client._send_init_sequence()

    test_content = """SEND_REQUEST {
    outbound.conn.
}"""

    open_message = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params":
            {
                "textDocument":
                    {
                        "uri": "file:///test_outbound_conn.hrw4u",
                        "languageId": "hrw4u",
                        "version": 1,
                        "text": test_content
                    }
            }
    }
    lsp_client.send_message(open_message)
    time.sleep(0.5)

    # Position cursor after "outbound.conn."
    completion_message = {
        "jsonrpc": "2.0",
        "id": 11,
        "method": "textDocument/completion",
        "params":
            {
                "textDocument": {
                    "uri": "file:///test_outbound_conn.hrw4u"
                },
                "position": {
                    "line": 1,
                    "character": 18  # After "outbound.conn."
                }
            }
    }

    lsp_client.send_message(completion_message)
    response = lsp_client.read_response(expect_id=11)

    assert response is not None, "Response should not be None"
    assert "result" in response, "Response should have result"
    assert "items" in response["result"], "Response should have items"
    items = response["result"]["items"]

    # Should find specific outbound.conn properties
    dscp_items = [item for item in items if item["label"] == "outbound.conn.dscp"]
    mark_items = [item for item in items if item["label"] == "outbound.conn.mark"]

    assert len(dscp_items) > 0, "outbound.conn.dscp should be available for completion"
    assert len(mark_items) > 0, "outbound.conn.mark should be available for completion"

    # Verify the details are correct
    dscp_item = dscp_items[0]
    mark_item = mark_items[0]

    assert "set-conn-dscp" in dscp_item["detail"], "DSCP should map to set-conn-dscp"
    assert "set-conn-mark" in mark_item["detail"], "Mark should map to set-conn-mark"
