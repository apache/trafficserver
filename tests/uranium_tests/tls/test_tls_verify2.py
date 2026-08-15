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

SSL_DIRECTORY = Path(__file__).parent / "ssl"


class TlsVerifyOverrideScenario:
    """Loosen a globally enforced origin certificate verification policy."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._foo = self.configure_origin(services, "server_foo", "foo")
        self._bar = self.configure_origin(services, "server_bar", "bar")
        self._default = services.origin("server", ssl=True)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory, name: str, certificate_name: str) -> OriginServer:
        """Create one signed HTTPS origin with valid and mismatched hosts."""

        origin = services.origin(
            name,
            ssl=True,
            clientkey=SSL_DIRECTORY / f"signed-{certificate_name}.key",
            clientcert=SSL_DIRECTORY / f"signed-{certificate_name}.pem",
        )
        response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"}
        for host in (f"{certificate_name}.com", f"bad_{certificate_name}.com"):
            origin.add_response({"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"}, response)
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure enforced defaults plus permissive and disabled SNI rules."""

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
                f"map https://foo.com/ https://127.0.0.1:{self._foo.https_port}",
                f"map https://bad_foo.com/ https://127.0.0.1:{self._foo.https_port}",
                f"map https://bar.com/ https://127.0.0.1:{self._bar.https_port}",
                f"map https://bad_bar.com/ https://127.0.0.1:{self._bar.https_port}",
                f"map / https://127.0.0.1:{self._default.https_port}",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
                "proxy.config.ssl.client.verify.server.properties": "ALL",
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: bar.com\n"
            "    verify_server_policy: PERMISSIVE\n"
            "    verify_server_properties: SIGNATURE\n"
            "  - fqdn: bad_bar.com\n"
            "    verify_server_policy: PERMISSIVE\n"
            "    verify_server_properties: SIGNATURE\n"
            "  - fqdn: random.com\n"
            "    verify_server_policy: DISABLED\n",
        )
        return ats

    def request(self, host: str) -> str:
        """Request @a host through the ATS TLS listener."""

        result = self._curl.run_for(
            self._ats,
            "--insecure",
            "--header",
            f"Host: {host}",
            f"https://127.0.0.1:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Exercise global and per-SNI verification modes."""

        self._foo.start()
        self._bar.start()
        self._default.start()
        self._ats.start()
        for host in ("foo.com", "random.com", "bar.com", "bad_bar.com"):
            assert "Could Not Connect" not in self.request(host)
        for host in ("random2.com", "bad_foo.com"):
            assert "Could Not Connect" in self.request(host)

        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "WARNING: SNI (bad_bar.com) not in certificate" not in diagnostics
        assert "WARNING: SNI (foo.com) not in certificate" not in diagnostics
        assert "Core server certificate verification failed for (random.com)" not in diagnostics
        assert "Core server certificate verification failed for (random2.com)" in diagnostics
        assert "WARNING: SNI (bad_foo.com) not in certificate" in diagnostics


def test_tls_verify2(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI rules can relax an enforced outbound verification policy."""

    TlsVerifyOverrideScenario(ats_factory, services, curl).run()
