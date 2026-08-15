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

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TEST_SSL = TEST_DIRECTORY.parents[1] / "tools" / "ssl"


class Http2WriteThresholdScenario:
    """Exercise the HTTP/2 write-size threshold and its timer."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        write_threshold: float,
        write_timeout: int,
    ) -> None:
        self._write_threshold = write_threshold
        self._write_timeout = write_timeout
        self._server_port = services.allocate_port()
        self._dns = self.configure_dns(services)
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve example.com to the local trickle server."""

        return services.dns("dns", default=["127.0.0.1"])

    def configure_server(self, services: ServiceFactory) -> ProcessService:
        """Create the TLS HTTP/2 server that trickles frames."""

        return services.process(
            "server",
            (
                sys.executable,
                TEST_DIRECTORY / "trickle_server.py",
                str(self._server_port),
                TEST_SSL / "server.pem",
                TEST_SSL / "server.key",
                str(self._write_timeout),
            ),
            ready_port=self._server_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure outbound H2 and the selected write thresholds."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.remap_config.add_line(f"map / https://example.com:{self._server_port}/")
        ats.records.update(
            {
                "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
                "proxy.config.http.server_session_sharing.pool": "thread",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http2.write_size_threshold": self._write_threshold,
                "proxy.config.http2.write_time_threshold": self._write_timeout,
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http",
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the HTTP/2 client that measures frame delivery timing."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "trickle_client.py",
                "example.com",
                str(self._ats.https_port),
                TEST_SSL / "server.pem",
                str(self._write_timeout),
            ),
        )

    def run(self) -> None:
        """Run the trickle transaction through ATS."""

        self._dns.start()
        self._server.start()
        self._ats.start()
        result = self._client.run(timeout=20)
        assert result.returncode == 0, result.output


def test_http2_write_threshold(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """proxy.config.http2.write_size_threshold flushes by size or timeout."""

    Http2WriteThresholdScenario(ats_factory, services, 0.5, 10).run()
