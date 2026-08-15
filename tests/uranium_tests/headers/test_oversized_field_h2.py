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
import ssl
import sys

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent
OVERSIZED_SIZE = 70000


class OversizedFieldH2Scenario:
    """Drive oversized HTTP/2 names and values with a raw HPACK client."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        if ssl.OPENSSL_VERSION_INFO < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        if not services.proxy_verifier_at_least("2.8.0"):
            pytest.skip("Proxy Verifier 2.8.0 or newer is required")
        self._services = services
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> VerifierServer:
        """Serve only the normal request and detect accidental oversized forwarding."""

        return services.verifier_server(
            "origin",
            "replay/oversized_field_h2.replay.yaml",
            https_ports=[],
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Raise the list limit while retaining the uint16 field-size ceiling."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2|hpack",
                "proxy.config.http.header_field_max_size": 65535,
                "proxy.config.http2.max_header_list_size": 8 * 1024 * 1024,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}")
        return ats

    def configure_client(self, name: str, path: str, name_size: int, value_size: int) -> ProcessService:
        """Configure one invocation of the raw HPACK client."""

        return self._services.process(
            name,
            (
                sys.executable,
                TEST_DIRECTORY / "clients" / "oversized_field_h2_client.py",
                path,
                str(name_size),
                str(value_size),
                "127.0.0.1",
                str(self._ats.https_port),
                "example.com",
            ),
        )

    @staticmethod
    def verify_rejection(result: CommandResult) -> None:
        """Require a connection error rather than an HTTP response."""

        assert result.returncode == 0, result.output
        assert "status=None" in result.output
        assert "goaway_error=9" in result.output

    @staticmethod
    def verify_normal(result: CommandResult) -> None:
        """Require an ordinary under-limit request to be proxied."""

        assert result.returncode == 0, result.output
        assert "status=200" in result.output

    def run(self) -> None:
        """Reject oversized value and name cases, then proxy the control request."""

        self._origin.start()
        self._ats.start()
        value_client = self.configure_client("oversized-value", "/h2-oversized-value", 0, OVERSIZED_SIZE)
        name_client = self.configure_client("oversized-name", "/h2-oversized-name", OVERSIZED_SIZE, 0)
        normal_client = self.configure_client("normal", "/h2-normal", 0, 0)
        self.verify_rejection(value_client.run(timeout=15))
        self.verify_rejection(name_client.run(timeout=15))
        self.verify_normal(normal_client.run(timeout=15))
        wait_for_file_lines(
            self._ats.diags_log,
            r"ERROR: HTTP/2 connection error code=0x09 .* compression error",
            2,
        )
        origin_output = self._origin.output
        assert re.search(r"h2-normal", origin_output)
        assert "h2-oversized-value" not in origin_output
        assert "h2-oversized-name" not in origin_output


def test_oversized_field_h2(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Oversized HTTP/2 fields produce GOAWAY COMPRESSION_ERROR and never reach the origin."""

    OversizedFieldH2Scenario(ats_factory, services).run()
