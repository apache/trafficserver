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
import sys

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsFlowControlScenario:
    """Verify that a slow TLS reader still receives the complete response."""

    _body_length = 8 * 1024 * 1024
    _high_water = 64 * 1024
    _low_water = 32 * 1024

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_server(self, services: ServiceFactory) -> OriginServer:
        """Create an origin response larger than the configured watermarks."""

        server = services.origin("server")
        server.add_response(
            {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                        f"Content-Length: {self._body_length}\r\n\r\n"),
                "body": "x" * self._body_length,
            },
        )
        return server

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable small HTTP tunnel flow-control watermarks over TLS."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.http.flow_control.enabled": 1,
                "proxy.config.http.flow_control.high_water": self._high_water,
                "proxy.config.http.flow_control.low_water": self._low_water,
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the deliberately slow TLS response reader."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "tls_flow_control_client.py",
                "-p",
                str(self._ats.https_port),
                "--host",
                "ex.test",
                "--path",
                "/obj",
                "--expect-bytes",
                str(self._body_length),
            ),
        )

    def run(self) -> None:
        """Run the slow reader and reject crashes or memory-safety failures."""

        self._server.start()
        self._ats.start()
        result = self._client.run(timeout=60)
        assert result.returncode == 0, result.output
        assert "RESULT=PASS" in result.output
        assert f"BODY_BYTES={self._body_length}" in result.output
        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        for expression in ("received signal", "failed assertion", "AddressSanitizer", "use-after-free", "runtime error:"):
            assert expression not in traffic_out


def test_tls_flow_control(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """TLS tunnel flow control throttles safely without stalling delivery."""

    TlsFlowControlScenario(ats_factory, services).run()
