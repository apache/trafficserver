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

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer


class H2OriginTrailersScenario:
    """Verify HTTP/2 origin trailers are safe for both client protocols."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._directory = Path(__file__).parent
        self._replay = self._directory / "h2_origin_trailers_h1.replay.yaml"

    def configure_server(self, name: str) -> VerifierServer:
        """Create an HTTP/2 TLS origin for one client-protocol case."""

        return self._services.verifier_server(name, self._replay)

    def configure_ats(self, name: str, server: VerifierServer) -> ATS:
        """Configure ATS to negotiate HTTP/2 with the origin."""

        ats = self._ats_factory.create(name, enable_tls=True, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|http2",
                "proxy.config.exec_thread.autoconfig.enabled": 0,
                "proxy.config.exec_thread.limit": 1,
                "proxy.config.http.server_session_sharing.pool": "thread",
                "proxy.config.http.server_session_sharing.match": "ip,sni,cert",
                "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_line(f"map / https://127.0.0.1:{server.https_port}")
        return ats

    def configure_h1_client(self, ats: ATS) -> ProcessService:
        """Create the raw client that detects trailers after the terminal chunk."""

        return self._services.process(
            "h1-client",
            [sys.executable, self._directory / "h1_trailer_client.py", "127.0.0.1",
             str(ats.http_port)],
        )

    def configure_h2_client(self, ats: ATS) -> ProcessService:
        """Create a verifier client that expects the HTTP/2 trailer."""

        return self._services.verifier_client("h2-client", self._replay, https_ports=[ats.https_port])

    def run(self) -> None:
        """Run the HTTP/1 and HTTP/2 client cases against separate ATS instances."""

        if not self._services.proxy_verifier_at_least("2.8.0"):
            pytest.skip("Proxy Verifier 2.8.0 or newer is required")

        h1_server = self.configure_server("h2-origin-h1")
        h1_ats = self.configure_ats("ts-h1", h1_server)
        h1_server.start()
        h1_ats.start()
        h1_result = self.configure_h1_client(h1_ats).run()
        assert "No H2 origin trailers were forwarded to the HTTP/1 client." in h1_result.stdout

        h2_server = self.configure_server("h2-origin-h2")
        h2_ats = self.configure_ats("ts-h2", h2_server)
        h2_server.start()
        h2_ats.start()
        h2_result = self.configure_h2_client(h2_ats).run()
        assert "x-ats-h2-trailer: smuggled" in h2_result.output


def test_h2_origin_trailers_h1(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HTTP/2 origin trailers are protocol-correct for HTTP/1 and HTTP/2 clients."""

    H2OriginTrailersScenario(ats_factory, services).run()
