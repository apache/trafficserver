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

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
HOST = "addressless.proxy.protocol.test"


class AddresslessProxyProtocolScenario:
    """Send an addressless PROXY header before clear-text or TLS HTTP."""

    def __init__(
        self,
        protocol_version: int,
        use_tls: bool,
        ats_factory: ATSFactory,
        services: ServiceFactory,
    ) -> None:
        self._protocol_version = protocol_version
        self._use_tls = use_tls
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve the request after ATS consumes the PROXY header."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": f"GET /proxy_protocol HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "ok"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable a PROXY-protocol listener for the selected transport."""

        ats = ats_factory.create(
            "ts",
            enable_tls=self._use_tls,
            enable_cache=False,
            enable_proxy_protocol=True,
        )
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.records.update(
            {
                "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "proxyprotocol",
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Configure the custom client for v1 UNKNOWN or v2 LOCAL."""

        port = self._ats.proxy_protocol_https_port if self._use_tls else self._ats.proxy_protocol_port
        arguments: list[str | Path] = [
            sys.executable,
            TEST_DIRECTORY / "proxy_protocol_client.py",
            "127.0.0.1",
            str(port),
            HOST,
            "127.0.0.1",
            "127.0.0.1",
            "60123",
            str(self._origin.port),
            str(self._protocol_version),
            "--addressless",
        ]
        if self._use_tls:
            arguments.append("--https")
        return services.process("client", arguments)

    def run(self) -> None:
        """Execute the custom client and require a successful response."""

        self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=10)
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.output


@pytest.mark.parametrize("protocol_version", [1, 2])
@pytest.mark.parametrize("use_tls", [False, True], ids=["http", "tls"])
def test_proxy_protocol_addressless(
    protocol_version: int,
    use_tls: bool,
    ats_factory: ATSFactory,
    services: ServiceFactory,
) -> None:
    """Addressless PROXY protocol headers are accepted before HTTP."""

    AddresslessProxyProtocolScenario(protocol_version, use_tls, ats_factory, services).run()
