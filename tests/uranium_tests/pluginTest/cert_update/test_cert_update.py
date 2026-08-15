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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class CertUpdateScenario:
    """Update inbound and outbound TLS certificates through cert_update."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._services = services
        self._curl = curl
        self._update_count = 0
        self._openssl_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("cert_update.so"):
            pytest.skip("cert_update.so is not installed")

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the clear-text origin used by the inbound certificate case."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the plugin and certificate mappings."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            SSL_DIRECTORY / "server1.pem",
            SSL_DIRECTORY / "server2.pem",
            SSL_DIRECTORY / "client1.pem",
            SSL_DIRECTORY / "client2.pem",
        )
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "cert_update|ssl_cert_update",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.verify.server.policy": "DISABLED",
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server1.pem",
                "    ssl_key_name: server1.pem",
            ))
        ats.remap_config.add_lines(
            (
                f"map https://bar.com http://127.0.0.1:{self._origin.http_port}",
                f"map https://foo.com https://127.0.0.1:{self._openssl_port}",
            ))
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            '  - fqdn: "*foo.com"\n'
            '    client_cert: "client1.pem"\n',
        )
        ats.plugin_config.add_line("cert_update.so")
        return ats

    def inbound_request(self) -> str:
        """Return curl's TLS diagnostics for ATS's current server certificate."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--insecure",
            "--resolve",
            f"bar.com:{self._ats.https_port}:127.0.0.1",
            f"https://bar.com:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        return result.stderr

    def update_certificate(self, target: str, path: Path) -> None:
        """Send one cert_update plugin message."""

        result = self._ats.traffic_ctl("plugin", "msg", f"cert_update.{target}", str(path))
        assert result.returncode == 0, result.output
        self._update_count += 1
        wait_for_file_lines(self._ats.traffic_out, "Successfully updated", self._update_count, timeout=10)

    def openssl_server(self, name: str, trusted_client: Path) -> ProcessService:
        """Create a one-shot TLS origin that requires an ATS client certificate."""

        certificate = self._ats.ssl_directory / "server1.pem"
        return self._services.process(
            name,
            (
                "openssl",
                "s_server",
                "-www",
                "-key",
                certificate,
                "-cert",
                certificate,
                "-CAfile",
                trusted_client,
                "-accept",
                str(self._openssl_port),
                "-Verify",
                "1",
                "-msg",
            ),
            ready_port=self._openssl_port,
        )

    def outbound_request(self, server: ProcessService) -> str:
        """Request the OpenSSL origin and return its handshake diagnostics."""

        server.start()
        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--insecure",
            "--header",
            "Host: foo.com",
            f"https://localhost:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        server.stop()
        return server.output

    def run(self) -> None:
        """Verify both certificate contexts change without restarting ATS."""

        self._origin.start()
        self._ats.start()
        assert "alice@bar.com" in self.inbound_request()
        self.update_certificate("server", self._ats.ssl_directory / "server2.pem")
        assert "bob@bar.com" in self.inbound_request()

        assert "alice.com" in self.outbound_request(self.openssl_server("s_server_before", SSL_DIRECTORY / "client1.pem"))
        (self._ats.ssl_directory / "client2.pem").replace(self._ats.ssl_directory / "client1.pem")
        self.update_certificate("client", self._ats.ssl_directory / "client1.pem")
        assert "bob.com" in self.outbound_request(self.openssl_server("s_server_after", SSL_DIRECTORY / "client2.pem"))
        assert "Successfully updated" in self._ats.traffic_out.read_text(errors="replace")


def test_cert_update(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """cert_update replaces server and client certificates at runtime."""

    CertUpdateScenario(ats_factory, services, curl).run()
