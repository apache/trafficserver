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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent


class ExpectContinueScenario:
    """Exercise the two-stage response with the test's purpose-built client."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._dns = self.configure_dns(services)
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the replay hostnames to the local verifier server."""

        return services.dns("dns", default="127.0.0.1")

    @staticmethod
    def configure_origin(services: ServiceFactory) -> VerifierServer:
        """Serve the final response after accepting the request body."""

        return services.verifier_server("origin", "replay/expect-continue.replay.yaml")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure ATS to generate the interim 100 Continue response."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.remap_config.add_line(f"map / http://backend.example.com:{self._origin.http_port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.send_100_continue_response": 1,
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Launch the ad hoc client that waits for 100 before sending its body."""

        return services.process(
            "expect-client",
            (
                sys.executable,
                TEST_DIRECTORY / "expect_client.py",
                "127.0.0.1",
                str(self._ats.http_port),
                "-s",
                "example.com",
            ),
        )

    def run(self) -> None:
        """Start dependencies, execute the client, and inspect both responses."""

        self._dns.start()
        self._origin.start()
        self._ats.start()
        result = self._client.run()
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 100" in result.stdout
        assert "HTTP/1.1 200" in result.stdout


def test_expect_continue(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS sends 100 Continue before forwarding the body to the origin."""

    if curl.uses_uds:
        pytest.skip("the purpose-built Expect client requires a TCP listener")
    ExpectContinueScenario(ats_factory, services).run()
