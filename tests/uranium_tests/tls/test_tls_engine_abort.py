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
import subprocess
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class TlsEngineAbortScenario:
    """Abort TLS handshakes while an OpenSSL asynchronous job is paused."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("the TLS abort client requires a TCP listener")
        openssl = subprocess.check_output(("openssl", "version"), text=True)
        version = re.search(r"\d+(?:\.\d+)+", openssl)
        if not openssl.startswith("OpenSSL") or version is None:
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        if tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        self._plugin = services.resolve_path("{AtsTestPluginsDir}/async_handshake.so")
        if not self._plugin.is_file():
            pytest.skip(f"{self._plugin} not found")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._abort_client = self.configure_client(services)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the normal response used after the abort barrage."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                        "Cache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n"),
                "body": "ok",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable asynchronous TLS handshakes with a two-second pause."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.async.handshake.enabled": 1,
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "ssl",
            })
        ats.copy_custom_plugin(self._plugin)
        ats.plugin_config.add_line("async_handshake.so -delay-ms=2000")
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the client that abandons thirty in-flight handshakes."""

        return services.process(
            "abort-client",
            (sys.executable, TEST_DIRECTORY / "tls_engine_abort.py", str(self._ats.https_port), "30"),
        )

    def run(self) -> None:
        """Run the barrage, then prove ATS can still serve a normal request."""

        self._origin.start()
        self._ats.start()
        aborts = self._abort_client.run(timeout=30)
        assert aborts.returncode == 0, aborts.output
        assert "sent 30 aborted handshakes" in aborts.output

        result = self._curl.run_for(
            self._ats,
            (f"--insecure --verbose --header uuid:basic --header host:example.com "
             f"'https://127.0.0.1:{self._ats.https_port}/'"),
            timeout=15,
        )
        assert result.returncode == 0, result.output
        assert re.search(r"HTTP/(2|1\.1) 200", result.output), result.output
        traffic_out = wait_for_file_lines(self._ats.traffic_out, "sent async wake signal to", 1, timeout=10)
        assert "AddressSanitizer" not in traffic_out


def test_tls_engine_abort(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Aborted asynchronous handshakes leave no stale poller registration."""

    TlsEngineAbortScenario(ats_factory, services, curl).run()
