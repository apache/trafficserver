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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsRenegotiationRefusedScenario:
    """Verify refused client renegotiation does not crash ATS."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        version = re.search(r"\d+(?:\.\d+)+", ssl.OPENSSL_VERSION)
        if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        if curl.uses_uds:
            pytest.skip("TLS renegotiation requires a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the normal response used after renegotiation is refused."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "ok"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Disable client renegotiation while retaining TLS 1.2."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.allow_client_renegotiation": 0,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.ssl.TLSv1_2": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl_load",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the TLS 1.2 client that requests renegotiation."""

        return services.process(
            "renegotiation-client",
            (
                sys.executable,
                TEST_DIRECTORY / "tls_renegotiation_client.py",
                "-p",
                str(self._ats.https_port),
                "-s",
                "example.com",
            ),
        )

    def run(self) -> None:
        """Refuse renegotiation, then serve an ordinary request."""

        self._origin.start()
        self._ats.start()
        renegotiation = self._client.run(timeout=20)
        assert renegotiation.returncode == 0, renegotiation.output

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--http1.1",
            "--tls-max",
            "1.2",
            "--tlsv1.2",
            "--ciphers",
            "DEFAULT@SECLEVEL=0",
            "--insecure",
            "--resolve",
            f"example.com:{self._ats.https_port}:127.0.0.1",
            f"https://example.com:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stderr
        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "received signal" not in traffic_out and "failed assertion" not in traffic_out
        if "BoringSSL" not in ssl.OPENSSL_VERSION:
            assert "trying to renegotiate from the client" in traffic_out


def test_tls_renegotiation(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Disallowed client renegotiation is refused without taking down ATS."""

    TlsRenegotiationRefusedScenario(ats_factory, services, curl).run()
