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
"""Validate truncated redirect framing using unmodified socket response bytes."""
import re
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory


def test_malformed_redirect_truncated(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Preserve valid redirects, refuse conflicting framing, and avoid poisoned pools.

    :param ats_factory: Proxy factory.
    :param services: Raw origin factory.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process(
        "origin", [sys.executable, directory / "malformed_redirect_truncated_server.py", "127.0.0.1",
                   str(port)], ready_port=port)
    for path in ("/truncated-302", "/pooled-conflicting-cl-302"):
        origin.stdout.contains(f"Received request line: GET {path}", "The origin must actually receive the regression request.")
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.http.server_session_sharing.pool": "global"})
    ats.remap_config.add_line(f"map http://www.example.com http://127.0.0.1:{port}")
    origin.start()
    ats.start()
    cases = (
        ("/truncated-302", ("HTTP/1.1 302 Found", "Location: http://www.example.com/elsewhere", "Content-Length: 0"), ()),
        ("/truncated-te-cl-302", ("HTTP/1.1 302 Found", "Transfer-Encoding: chunked"), ("Content-Length:",)),
        ("/truncated-dup-cl-302", ("HTTP/1.1 302 Found", "Content-Length: 5"), ()),
        ("/truncated-conflicting-cl-302", ("HTTP/1.1 502",), ("Location:", "Content-Length: 11")),
        ("/pooled-conflicting-cl-302", ("HTTP/1.1 502",), ("Location:",)),
        ("/well-formed-302", ("HTTP/1.1 302 Found", "Content-Length: 5"), ("world",)),
        ("/truncated-dup-cl-302", ("Content-Length: 5",), ()),
    )
    for index, (path, contains, excludes) in enumerate(cases):
        request = ats.run_directory / f"request-{index}.txt"
        request.write_bytes(f"GET {path} HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n".encode())
        result = ats.run(
            sys.executable, directory.parents[1] / "tools/tcp_client.py", "127.0.0.1", str(ats.http_port), request, timeout=30)
        assert result.returncode == 0, result.output
        for expression in contains:
            assert expression in result.stdout, result.output
        for expression in excludes:
            assert expression not in result.stdout, result.output
        if path == "/truncated-dup-cl-302":
            assert len(re.findall(r"^Content-Length: 5\r?$", result.stdout, re.MULTILINE)) == 1, result.output
        assert origin.is_running
