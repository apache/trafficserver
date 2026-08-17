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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsVerifyWildcardScenario:
    """Override outbound verification with exact and wildcard SNI rules."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._foo = self.configure_origin(services, "server_foo", "foo")
        self._bar = self.configure_origin(services, "server_bar", "bar")
        self._default = services.origin("server", ssl=True)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory, name: str, certificate_name: str) -> OriginServer:
        """Create one signed HTTPS origin."""

        origin = services.origin(
            name,
            ssl=True,
            clientkey=SSL_DIRECTORY / f"signed-{certificate_name}.key",
            clientcert=SSL_DIRECTORY / f"signed-{certificate_name}.pem",
        )
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: {certificate_name}.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure global permissive verification and wildcard overrides."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            SSL_DIRECTORY / "signed-foo.pem",
            SSL_DIRECTORY / "signed-foo.key",
            SSL_DIRECTORY / "signed-bar.pem",
            SSL_DIRECTORY / "signed-bar.key",
            SSL_DIRECTORY / "server.pem",
            SSL_DIRECTORY / "server.key",
            SSL_DIRECTORY / "signer.pem",
            SSL_DIRECTORY / "signer.key",
        )
        ats.remap_config.add_lines(
            (
                f"map https://foo.com:{ats.https_port}/ https://127.0.0.1:{self._foo.https_port}",
                f"map https://bob.foo.com:{ats.https_port}/ https://127.0.0.1:{self._foo.https_port}",
                f"map https://bar.com:{ats.https_port}/ https://127.0.0.1:{self._bar.https_port}",
                f"map https://bob.bar.com:{ats.https_port}/ https://127.0.0.1:{self._bar.https_port}",
                f"map / https://127.0.0.1:{self._default.https_port}",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.ssl.client.verify.server.properties": "ALL",
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: bob.bar.com\n"
            "    verify_server_policy: ENFORCED\n"
            "    verify_server_properties: ALL\n"
            '  - fqdn: "*.foo.com"\n'
            "    verify_server_policy: ENFORCED\n"
            "    verify_server_properties: SIGNATURE\n"
            '  - fqdn: "*.bar.com"\n'
            "    verify_server_policy: DISABLED\n",
        )
        return ats

    def request(self, hostname: str) -> str:
        """Request @a hostname through the ATS TLS listener."""

        result = self._curl.run(
            (
                f"--verbose --insecure --resolve '{hostname}:{self._ats.https_port}:127.0.0.1' "
                f"'https://{hostname}:{self._ats.https_port}/'"),)
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Exercise exact, wildcard, disabled, and default SNI policies."""

        self._foo.start()
        self._bar.start()
        self._default.start()
        self._ats.start()
        assert "Could Not Connect" not in self.request("foo.com")
        assert "Could Not Connect" not in self.request("my.random.com")
        assert "Could Not Connect" in self.request("bob.bar.com")
        assert "Could Not Connect" not in self.request("bob.foo.com")
        assert "Could Not Connect" not in self.request("random.bar.com")
        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "WARNING: SNI (bob.bar.com) not in certificate" in diagnostics
        assert "WARNING: Core server certificate verification failed for (my.random.com). Action=Continue" in diagnostics


def test_tls_verify3(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Wildcard SNI rules override the global outbound verification policy."""

    TlsVerifyWildcardScenario(ats_factory, services, curl).run()
