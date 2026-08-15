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

import pytest

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class H2SpecScenario:
    """Run the HTTP/2 conformance client against an ATS TLS listener."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> HttpBinServer:
        """Serve the generic resources requested by h2spec."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Expose an uncached TLS endpoint with Via headers enabled."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.http.insert_request_via_str": 1,
                "proxy.config.http.insert_response_via_str": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Select the generic, framing, stream, and HPACK conformance groups."""

        targets = ("generic", "http2/3", "http2/4", "http2/5", "http2/6", "http2/7", "http2/8", "hpack")
        return services.process(
            "h2spec",
            ("h2spec", *targets, "-t", "-k", "--timeout", "10", "-p", str(self._ats.https_port)),
        )

    def run(self) -> None:
        """Run the conformance suite and validate its summary and ATS diagnostics."""

        self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=120)
        assert_matches_gold(result.stdout, TEST_DIRECTORY / "gold" / "h2spec_stdout.gold")
        assert "ERROR: HTTP/2" in self._ats.diags_log.read_text(errors="replace")


def test_h2spec(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS passes the selected h2spec conformance groups."""

    if shutil.which("h2spec") is None:
        pytest.skip("h2spec is required")
    H2SpecScenario(ats_factory, services).run()
