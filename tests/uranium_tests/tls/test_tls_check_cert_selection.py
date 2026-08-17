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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsCertSelectionScenario:
    """Verify SNI, address, and default inbound certificate selection."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("SNI certificate selection requires a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the HTTPS origin used after the inbound handshake."""

        origin = services.origin("server", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve the outbound foo.com origin."""

        dns = services.dns("dns")
        dns.add_records({"foo.com": ["127.0.0.1"], "bar.com": ["127.0.0.1"]})
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install address-specific, SNI, and fallback certificates."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            *(
                TEST_DIRECTORY / "ssl" / name for name in (
                    "signed-foo.pem",
                    "signed-foo.key",
                    "signed-bar.pem",
                    "signed2-bar.pem",
                    "signed-bar.key",
                    "signer.pem",
                    "signer.key",
                    "combo.pem",
                )))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "127.0.0.1"',
                "    ssl_cert_name: signed-foo.pem",
                "    ssl_key_name: signed-foo.key",
                "  - ssl_cert_name: signed2-bar.pem",
                "    ssl_key_name: signed-bar.key",
                '  - dest_ip: "*"',
                "    ssl_cert_name: combo.pem",
            ))
        ats.remap_config.add_line(f"map / https://foo.com:{self._origin.https_port}")
        ats.records.update(
            {
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        return ats

    def request(self, hostname: str, ca_file: str | None = None) -> str:
        """Connect with @a hostname and return curl's handshake diagnostics."""

        arguments = ["--verbose"]
        if ca_file is None:
            arguments.append("--insecure")
        else:
            arguments.extend(("--cacert", str(TEST_DIRECTORY / "ssl" / ca_file)))
        arguments.extend(
            (
                "--resolve",
                f"{hostname}:{self._ats.https_port}:127.0.0.1",
                f"https://{hostname}:{self._ats.https_port}",
            ))
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.output
        return result.output

    def run(self) -> None:
        """Verify named, fallback, and IP-specific certificate selection."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        bar = self.request("bar.com", "signer2.pem")
        assert "CN=bar.com" in bar and "CN=foo.com" not in bar
        foo = self.request("foo.com", "signer.pem")
        assert "CN=foo.com" in foo and "CN=bar.com" not in foo
        fallback = self.request("random.server.com")
        assert "CN=random.server.com" in fallback
        assert "CN=foo.com" not in fallback and "CN=bar.com" not in fallback
        bad_sni = self.request("bad.sni.com")
        assert "CN=foo.com" in bad_sni and "CN=bar.com" not in bad_sni


def test_tls_check_cert_selection(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS offers the correct certificate for SNI and destination address."""

    TlsCertSelectionScenario(ats_factory, services, curl).run()
