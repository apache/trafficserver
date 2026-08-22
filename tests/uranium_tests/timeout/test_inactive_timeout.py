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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class InactivityTimeoutScenario:
    """Delay the origin beyond ATS's outbound transaction timeout."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Delay the origin response long enough for ATS to time out."""

        origin = services.origin("origin", delay=8)
        origin.add_response(
            {
                "headers": "GET /file HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
            "sessionfile.log",
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable HTTP and TLS listeners with a two-second origin timeout."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.records.update(
            {
                "proxy.config.url_remap.remap_required": 1,
                "proxy.config.http.transaction_no_activity_timeout_out": 2,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    @staticmethod
    def verify_timeout(result: CommandResult) -> None:
        """Require the ATS timeout response body."""

        assert result.returncode == 0, result.output
        assert "Inactivity Timeout" in result.stdout

    def run(self) -> None:
        """Exercise clear-text, HTTP/1.1 TLS, and HTTP/2 TLS clients."""

        self._origin.start()
        self._ats.start()
        self.verify_timeout(self._curl.get(self._ats, "/file", options=f"--include", timeout=20))
        if self._curl.uses_uds:
            return
        for protocol in ("--http1.1", "--http2"):
            result = self._curl.run(
                f"--insecure --include '{protocol}' 'https://127.0.0.1:{self._ats.https_port}/file'",
                timeout=20,
            )
            self.verify_timeout(result)


def test_inactive_timeout(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns an inactivity timeout for stalled origin responses."""

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    InactivityTimeoutScenario(ats_factory, services, curl).run()
