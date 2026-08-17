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
import re
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class PluginCertSelectionScenario:
    """Load and refresh SNI certificates through the secret hook plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("SNI certificate selection requires a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the HTTPS origin reached after inbound TLS selection."""

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
        return origin

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the outbound origin name."""

        dns = services.dns("dns")
        dns.add_records({"foo.com.": ["127.0.0.1"], "bar.com.": ["127.0.0.1"]})
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure address, SNI, and fallback certificates through the hook."""

        ats = ats_factory.create("ts", enable_tls=True)
        names = (
            "signed-foo.pem",
            "signed-foo.key",
            "signed-bar.pem",
            "signed2-bar.pem",
            "signed-bar.key",
            "server.pem",
            "server.key",
            "signer.pem",
            "signer.key",
        )
        ats.copy_to_ssl(*(SSL_DIRECTORY / name for name in names))
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
        ats.plugin_config.add_line("ssl_secret_load_test.so")
        ats.remap_config.add_line(f"map / https://foo.com:{self._origin.https_port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "127.0.0.1"',
                "    ssl_cert_name: signed-foo.pem",
                "    ssl_key_name: signed-foo.key",
                "  - ssl_cert_name: signed2-bar.pem",
                "    ssl_key_name: signed-bar.key",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.tags": "ssl_secret_load_test|ssl",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory.parent),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory.parent),
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        return ats

    def request(self, hostname: str, ca_file: str | None = None, expected_code: int = 0) -> str:
        """Connect to one SNI name and return curl's TLS diagnostics."""

        arguments = ["--verbose"]
        if ca_file is None:
            arguments.append("--insecure")
        else:
            arguments.extend(("--cacert", str(SSL_DIRECTORY / ca_file)))
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
        assert result.returncode == expected_code, result.output
        return result.output

    def refresh_bar_certificate(self) -> None:
        """Replace the watched bar certificate and await the plugin poll."""

        time.sleep(1.1)
        live = self._ats.ssl_directory / "signed2-bar.pem"
        shutil.copyfile(SSL_DIRECTORY / "signed-bar.pem", live)
        live.touch()
        time.sleep(4)

    def run(self) -> None:
        """Verify initial selection, fallback, and automatic secret refresh."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        bar = self.request("bar.com", "signer2.pem")
        assert "CN=bar.com" in bar and "CN=foo.com" not in bar and re.search(r"HTTP/[\d.]+ 404", bar)
        foo = self.request("foo.com", "signer.pem")
        assert "CN=foo.com" in foo and "CN=bar.com" not in foo and re.search(r"HTTP/[\d.]+ 404", foo)
        fallback = self.request("random.server.com")
        assert "CN=random.server.com" in fallback and re.search(r"HTTP/[\d.]+ 404", fallback)
        bad_sni = self.request("bad.sni.com")
        assert "CN=foo.com" in bad_sni and "CN=bar.com" not in bad_sni

        self.refresh_bar_certificate()
        refreshed = self.request("bar.com", "signer.pem")
        assert "CN=bar.com" in refreshed and re.search(r"HTTP/[\d.]+ 404", refreshed)
        rejected = self.request("bar.com", "signer2.pem", 60)
        assert "curl: (60) SSL certificate" in rejected


def test_tls_check_cert_select_plugin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The TLS secret hook selects and refreshes inbound certificates."""

    PluginCertSelectionScenario(ats_factory, services, curl).run()
