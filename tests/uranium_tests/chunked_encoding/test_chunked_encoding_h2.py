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

from pathlib import Path
import shutil
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class ChunkedEncodingH2Scenario:
    """Exercise chunked H1 origin traffic behind an HTTP/2 client."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("HTTP/2 TLS coverage requires a TCP listener")
        if shutil.which("nghttp") is None:
            pytest.skip("nghttp is required")
        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        self._curl = curl
        self._sandbox = ats_factory.run_directory
        self._ports = [services.allocate_port() for _ in range(3)]
        self._outputs = [self._sandbox / f"outserver{number}" for number in range(1, 4)]
        self._servers = self.configure_servers(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_servers(self, services: ServiceFactory) -> list[ProcessService]:
        """Create raw origins for delayed, fixed-length, and chunked responses."""

        servers = []
        for number, (port, output, response) in enumerate(zip(self._ports, self._outputs,
                                                              ("delayed-chunked", "content-length", "chunked")), 1):
            server = services.process(
                f"server{number}",
                (
                    sys.executable,
                    TEST_DIRECTORY / "chunked_encoding_h2_server.py",
                    "127.0.0.1",
                    str(port),
                    output,
                    response,
                ),
                ready_port=port,
            )
            servers.append(server)
        return servers

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS ingress and one route for each raw origin."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        for path, port in zip(("delay-chunked-response", "post-full", "post-chunked"), self._ports):
            ats.remap_config.add_line(f"map /{path} http://127.0.0.1:{port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        return ats

    def run(self) -> None:
        """Verify response framing and H2-to-H1 request body conversion."""

        for server in self._servers:
            server.start()
        self._ats.start()
        delayed = self._ats.run("nghttp", "-vv", f"https://127.0.0.1:{self._ats.https_port}/delay-chunked-response", timeout=15)
        assert delayed.returncode == 0, delayed.output
        assert "RST_STREAM" not in delayed.output
        assert "< content-length" not in delayed.output
        assert ":status: 200" in delayed.output

        for path, expects_content_length in (("post-full", True), ("post-chunked", False)):
            result = self._curl.run_for(
                self._ats,
                "--http2",
                "--insecure",
                "--verbose",
                "--header",
                "Transfer-encoding: chunked",
                "--data",
                "Knock knock",
                f"https://127.0.0.1:{self._ats.https_port}/{path}",
            )
            assert result.returncode == 0, result.output
            assert "HTTP/2 200" in result.output
            assert ("< content-length:" in result.output) is expects_content_length
        for output in self._outputs[1:]:
            assert "Knock knock" in output.read_text(errors="replace")


def test_chunked_encoding_h2(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP/2 transactions preserve correct chunked framing at an H1 origin."""

    ChunkedEncodingH2Scenario(ats_factory, services, curl).run()
