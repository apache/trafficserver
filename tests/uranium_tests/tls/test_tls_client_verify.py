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

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsClientVerifyScenario:
    """Require client certificates by default and override the policy by SNI."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the clear-text origin used after successful handshakes."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure strict global verification and the SNI exception matrix."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "signer.pem")
        ats.set_ssl_multicert_yaml(
            {"ssl_multicert": [{
                "dest_ip": "*",
                "ssl_cert_name": "server.pem",
                "ssl_key_name": "server.key"
            }]})
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.client.certification_level": 2,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: bob.bar.com\n"
            "    verify_client: NONE\n"
            "  - fqdn: bob.com\n"
            "    verify_client: STRICT\n"
            "  - fqdn: '*.foo.com'\n"
            "    verify_client: NONE\n"
            "  - fqdn: '*.bar.com'\n"
            "    verify_client: STRICT\n",
        )
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "testformat",
                            "format": "%<pssc> %<pquc> %<pscert> %<cscert>"
                        }],
                        "logs": [{
                            "mode": "ascii",
                            "format": "testformat",
                            "filename": "squid"
                        }],
                    }
            })
        return ats

    def request(
        self,
        host: str,
        case_number: int,
        certificate: str | None = None,
        key: str | None = None,
    ) -> CommandResult:
        """Connect to one SNI name with optional client certificate material."""

        arguments = ["--tls-max", "1.2", "--insecure"]
        if certificate is not None and key is not None:
            arguments.extend(("--cert", str(SSL_DIRECTORY / certificate), "--key", str(SSL_DIRECTORY / key)))
        arguments.extend(
            (
                "--resolve",
                f"{host}:{self._ats.https_port}:127.0.0.1",
                f"https://{host}:{self._ats.https_port}/case{case_number}",
            ))
        return self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )

    def run(self) -> None:
        """Run the policy matrix and verify certificate-presence access-log fields."""

        self._origin.start()
        self._ats.start()
        cases = (
            ("foo.com", 1, None, None, False),
            ("foo.com", 2, "server.pem", "server.key", False),
            ("foo.com", 3, "signed-foo.pem", "signed-foo.key", True),
            ("bob.bar.com", 4, None, None, True),
            ("bob.bar.com", 5, "signed-bob-bar.pem", "signed-bar.key", True),
            ("bob.bar.com", 6, "server.pem", "server.key", True),
            ("bob.foo.com", 7, None, None, True),
            ("bob.foo.com", 8, "signed-bob-foo.pem", "signed-foo.key", True),
            ("bob.foo.com", 9, "server.pem", "server.key", True),
            ("bar.com", 10, None, None, False),
            ("bar.com", 11, "signed-bar.pem", "signed-bar.key", True),
            ("bar.com", 12, "server.pem", "server.key", False),
            ("bob.com", 13, None, None, False),
            ("bob.foo.com", 14, None, None, True),
        )
        for host, case_number, certificate, key, should_succeed in cases:
            result = self.request(host, case_number, certificate, key)
            if should_succeed:
                assert result.returncode == 0, result.output
            else:
                assert result.returncode != 0, result.output

        access_log = wait_for_file_lines(self._ats.log_directory / "squid.log", r"^404 ", 9)
        assert_matches_gold(access_log, TEST_DIRECTORY / "gold" / "clientcert-accesslog.gold")


def test_tls_client_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Global and SNI client-certificate policies produce the expected handshakes and logs."""

    TlsClientVerifyScenario(ats_factory, services, curl).run()
