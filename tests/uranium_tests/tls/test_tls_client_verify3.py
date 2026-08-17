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


class TlsClientVerifyCaScenario:
    """Select inbound client-certificate CAs from the requested SNI name."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the clear-text origin behind the TLS endpoint."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET /xyz HTTP/1.1\r\nHost: example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "yadayadayada",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the default AAA CA and per-SNI BBB and CCC CAs."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_config(SSL_DIRECTORY / "bbb-ca.pem")
        ats.copy_to_ssl(
            SSL_DIRECTORY / "server.pem",
            SSL_DIRECTORY / "server.key",
            SSL_DIRECTORY / "bbb-signed.key",
            SSL_DIRECTORY / "bbb-signed.pem",
            SSL_DIRECTORY / "aaa-ca.pem",
            SSL_DIRECTORY / "ccc-ca.pem",
        )
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                "  - ssl_cert_name: bbb-signed.pem",
                "    ssl_key_name: bbb-signed.key",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.client.certification_level": 2,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "aaa-ca.pem"),
                "proxy.config.ssl.TLSv1_3.enabled": 0,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: bbb.com\n"
            "    verify_client: STRICT\n"
            "    verify_client_ca_certs: bbb-ca.pem\n"
            "  - fqdn: bbb-signed\n"
            "    verify_client: STRICT\n"
            "    verify_client_ca_certs: bbb-ca.pem\n"
            "  - fqdn: ccc.com\n"
            "    verify_client: STRICT\n"
            "    verify_client_ca_certs:\n"
            f"      file: {ats.ssl_directory / 'ccc-ca.pem'}\n",
        )
        return ats

    def request(self, hostname: str, certificate_name: str) -> tuple[int, str]:
        """Connect with @a certificate_name while requesting @a hostname."""

        result = self._curl.run(
            (
                f"--verbose --insecure --tls-max 1.2 --cert '{str(SSL_DIRECTORY / f'{certificate_name}.pem')}' --key "
                f"'{str(SSL_DIRECTORY / f'{certificate_name}.key')}' --resolve "
                f"'{hostname}:{self._ats.https_port}:127.0.0.1' 'https://{hostname}:{self._ats.https_port}/xyz'"),)
        return result.returncode, result.output

    def run(self) -> None:
        """Exercise matching and mismatched CA selections."""

        self._origin.start()
        self._ats.start()
        for hostname, certificate in (
            ("aaa.com", "aaa-signed"),
            ("bbb-signed", "bbb-signed"),
            ("ccc.com", "ccc-signed"),
        ):
            return_code, output = self.request(hostname, certificate)
            assert return_code == 0, output
            assert "yadayadayada" in output
        for hostname, certificate in (
            ("aaa.com", "bbb-signed"),
            ("bbb.com", "ccc-signed"),
            ("ccc.com", "aaa-signed"),
        ):
            return_code, output = self.request(hostname, certificate)
            assert return_code != 0, output


def test_tls_client_verify3(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI policy chooses the CA used to validate an inbound client certificate."""

    TlsClientVerifyCaScenario(ats_factory, services, curl).run()
