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


class ActiveTimeoutScenario:
    """Delay the origin beyond ATS's outbound transaction active timeout."""

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
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable all available client protocols with a two-second timeout."""

        enable_quic = ats_factory.has_feature("TS_USE_QUIC")
        ats = ats_factory.create("ts", enable_tls=True, enable_quic=enable_quic)
        ats.records.update({
            "proxy.config.url_remap.remap_required": 1,
            "proxy.config.http.transaction_active_timeout_out": 2,
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    @staticmethod
    def verify_timeout(result: CommandResult) -> None:
        """Require the ATS active-timeout response body."""

        assert result.returncode == 0, result.output
        assert "Activity Timeout" in result.stdout

    def run(self) -> None:
        """Exercise every client protocol supported by this build."""

        self._origin.start()
        self._ats.start()
        self.verify_timeout(self._curl.get(self._ats, "/file", options=("--include",), timeout=20))
        if self._curl.uses_uds:
            return
        for protocol in ("--http1.1", "--http2"):
            self.verify_timeout(
                self._curl.run(
                    "--insecure",
                    "--include",
                    protocol,
                    f"https://127.0.0.1:{self._ats.https_port}/file",
                    timeout=20,
                ))
        if self._ats.has_feature("TS_USE_QUIC") and self._curl.supports("http3"):
            self.verify_timeout(
                self._curl.run(
                    "--insecure",
                    "--include",
                    "--http3",
                    f"https://localhost:{self._ats.https_port}/file",
                    timeout=20,
                ))


def test_active_timeout(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns an active timeout for stalled origin responses."""

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    ActiveTimeoutScenario(ats_factory, services, curl).run()
