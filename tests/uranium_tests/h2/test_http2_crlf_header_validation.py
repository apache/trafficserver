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
import re
import sys

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory


class InvalidH2HeaderScenario:
    """Send HTTP/2 header values containing forbidden control characters."""

    CASES = (
        "crlf-in-header-value",
        "cr-in-header-value",
        "lf-in-header-value",
        "nul-in-header-value",
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._client = Path(__file__).parent.parent / "connect" / "malformed_h2_request_client.py"
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create an origin used to detect accidentally forwarded requests."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a TLS HTTP/2 listener in front of the origin."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    def run_client(self, scenario: str) -> None:
        """Run one raw-wire malformed-header case."""

        result = self._services.process(
            f"client-{scenario}",
            [sys.executable, self._client, str(self._ats.https_port), scenario],
        ).run()
        assert re.search(r"Received (RST_STREAM|GOAWAY|HTTP/2 response with status 4\d\d)", result.stdout), result.output

    def run(self) -> None:
        """Verify all malformed values are rejected before reaching the origin."""

        self._origin.start()
        self._ats.start()
        for scenario in self.CASES:
            self.run_client(scenario)
        assert "x-injected" not in self._origin.output
        assert "malformed-nul-value" not in self._origin.output


def test_http2_crlf_header_validation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HTTP/2 requests with NUL, CR, or LF header values are rejected."""

    InvalidH2HeaderScenario(ats_factory, services).run()
