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
import shlex

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class TlsKeepaliveScenario:
    """Verify TLS session reuse for H1 and H2 connection patterns."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("TLS keep-alive coverage requires a TCP listener")
        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the reusable empty-response origin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS access logging and the pre-accept hook plugin."""

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
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map https://example.com:{ats.https_port} http://127.0.0.1:{self._origin.port}")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "testformat",
                            "format": "%<cqssl> %<cqtr>"
                        }],
                        "logs": [{
                            "mode": "ascii",
                            "format": "testformat",
                            "filename": "squid"
                        }],
                    }
            })
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
        ats.plugin_config.add_line("ssl_secret_load_test.so")
        return ats

    def curl_arguments(self, protocol: str) -> tuple[str, ...]:
        """Return common curl arguments for @a protocol."""

        return (
            "--insecure",
            "--verbose",
            f"--{protocol}",
            "--header",
            f"host:example.com:{self._ats.https_port}",
        )

    def request_pair(self, protocol: str, *, same_connection: bool) -> None:
        """Issue two requests on either one or two client connections."""

        url = f"https://127.0.0.1:{self._ats.https_port}"
        arguments = self.curl_arguments(protocol)
        if same_connection:
            result = self._curl.run_for(
                self._ats,
                f"{shlex.join(arguments)} '{url}' '{url}'",
            )
            assert result.returncode == 0, result.output
        else:
            for _ in range(2):
                result = self._curl.run_for(
                    self._ats,
                    f"{shlex.join(arguments)} '{url}'",
                )
                assert result.returncode == 0, result.output

    def run(self) -> None:
        """Exercise the four connection/protocol combinations and check the log."""

        self._origin.start()
        self._ats.start()
        self.request_pair("http1.1", same_connection=True)
        self.request_pair("http1.1", same_connection=False)
        self.request_pair("http2", same_connection=True)
        self.request_pair("http2", same_connection=False)
        access_log = self._ats.log_directory / "squid.log"
        content = wait_for_file_lines(access_log, r"^1 [01]$", 8, timeout=10)
        assert_matches_gold(content, TEST_DIRECTORY / "gold" / "accesslog.gold")


def test_tls_keepalive(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """TLS keep-alive is honored for HTTP/1.1 and HTTP/2 clients."""

    TlsKeepaliveScenario(ats_factory, services, curl).run()
