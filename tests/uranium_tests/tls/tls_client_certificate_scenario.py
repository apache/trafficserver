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
import shutil
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsClientCertificateScenario:
    """Select and reload outbound client certificates using records and SNI."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        use_secret_plugin: bool,
        wildcard_matrix: bool,
    ) -> None:
        self._curl = curl
        self._use_secret_plugin = use_secret_plugin
        self._wildcard_matrix = wildcard_matrix
        self._first = self.configure_origin(services, "first-origin", "signer.pem", "signed-foo.pem", respond=True)
        self._second = self.configure_origin(services, "second-origin", "signer2.pem", "signed2-bar.pem", respond=False)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(
        services: ServiceFactory,
        name: str,
        client_ca: str,
        certificate: str,
        *,
        respond: bool,
    ) -> OriginServer:
        """Create an HTTPS origin that requires a client certificate from one CA."""

        origin = services.origin(
            name,
            ssl=True,
            clientcert=SSL_DIRECTORY / certificate,
            clientkey=SSL_DIRECTORY / ("signed-bar.key" if "bar" in certificate else "signed-foo.key"),
            options={
                "--clientCA": SSL_DIRECTORY / client_ca,
                "--clientverify": ""
            },
        )
        if respond:
            for host in ("example.com", "bar.com"):
                origin.add_response(
                    {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
                    {
                        "headers": "HTTP/1.1 200 OK\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
                    },
                )
        return origin

    def certificate_reference(self, ats: ATS, filename: str) -> str:
        """Return the path form expected by ATS or the secret-loader test plugin."""

        directory = ats.ssl_directory.parent if self._use_secret_plugin else ats.ssl_directory
        return str(directory / filename)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure certificate material, SNI selection, remap, and optional logging."""

        ats = ats_factory.create("ts")
        files = (
            "server.pem",
            "server.key",
            "combo-signed-foo.pem",
            "signed-foo.pem",
            "signed-foo.key",
            "signed2-foo.pem",
            "signed-bar.pem",
            "signed2-bar.pem",
            "signed-bar.key",
        )
        ats.copy_to_ssl(*(SSL_DIRECTORY / filename for filename in files))
        client_directory = ats.ssl_directory.parent if self._use_secret_plugin else ats.ssl_directory
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl_secret_load_test|ssl" if self._use_secret_plugin else "ssl",
                "proxy.config.ssl.client.cert.path": str(client_directory),
                "proxy.config.ssl.client.private_key.path": str(client_directory),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        if not self._wildcard_matrix:
            ats.records.update(
                {
                    "proxy.config.ssl.client.cert.filename": "signed-foo.pem",
                    "proxy.config.ssl.client.private_key.filename": "signed-foo.key",
                })
        if self._use_secret_plugin:
            ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
            ats.plugin_config.add_line("ssl_secret_load_test.so")
        ats.remap_config.add_line(f"map /case1 https://127.0.0.1:{self._first.https_port}/")
        ats.remap_config.add_line(f"map /case2 https://127.0.0.1:{self._second.https_port}/")
        ats.write_config_file("sni.yaml", self.initial_sni(ats))
        if not self._use_secret_plugin:
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

    def initial_sni(self, ats: ATS) -> str:
        """Render either the basic host selection or wildcard precedence matrix."""

        if not self._wildcard_matrix:
            return (
                "sni:\n"
                "  - fqdn: bar.com\n"
                f"    client_cert: {self.certificate_reference(ats, 'signed2-bar.pem')}\n"
                f"    client_key: {self.certificate_reference(ats, 'signed-bar.key')}\n")
        return (
            "sni:\n"
            "  - fqdn: bob.bar.com\n"
            f"    client_cert: {self.certificate_reference(ats, 'signed-bar.pem')}\n"
            f"    client_key: {self.certificate_reference(ats, 'signed-bar.key')}\n"
            "  - fqdn: bob.foo.com\n"
            f"    client_cert: {self.certificate_reference(ats, 'combo-signed-foo.pem')}\n"
            "  - fqdn: '*.bar.com'\n"
            f"    client_cert: {self.certificate_reference(ats, 'signed2-bar.pem')}\n"
            f"    client_key: {self.certificate_reference(ats, 'signed-bar.key')}\n"
            "  - fqdn: foo.com\n"
            f"    client_cert: {self.certificate_reference(ats, 'signed2-foo.pem')}\n"
            f"    client_key: {self.certificate_reference(ats, 'signed-foo.key')}\n")

    def request(self, host: str, path: str, succeeds: bool) -> None:
        """Send one request and verify whether the outbound handshake succeeded."""

        result = self._curl.get(self._ats, path, headers={"Host": host})
        assert result.returncode == 0, result.output
        if succeeds:
            assert "Could Not Connect" not in result.stdout, result.output
        else:
            assert "Could Not Connect" in result.stdout, result.output

    def verify_wildcard_matrix(self) -> None:
        """Check exact names, wildcard names, and nonmatching names."""

        cases = (
            ("bob.bar.com", "/case1", True),
            ("bob.bar.com", "/case2", False),
            ("bob.foo.com", "/case1", True),
            ("bob.foo.com", "/case2", False),
            ("random.bar.com", "/case2", True),
            ("random.bar.com", "/case1", False),
            ("random.foo.com", "/case2", False),
            ("random.foo.com", "/case1", False),
        )
        for host, path, succeeds in cases:
            self.request(host, path, succeeds)

    def reload_configuration(self, default_certificate: str, bar_certificate: str) -> None:
        """Change both default and SNI-selected certificate names, then reload ATS."""

        self._ats.traffic_ctl("config", "set", "proxy.config.ssl.client.cert.filename", default_certificate)
        (self._ats.config_directory / "sni.yaml").write_text(
            "sni:\n"
            "  - fqdn: bar.com\n"
            f"    client_cert: {self.certificate_reference(self._ats, bar_certificate)}\n"
            f"    client_key: {self.certificate_reference(self._ats, 'signed-bar.key')}\n")
        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output

    def verify_reload_matrix(self) -> None:
        """Verify config-name reloads and in-place certificate content changes."""

        self.request("example.com", "/case1", True)
        self.request("example.com", "/case2", False)
        self.request("bar.com", "/case2", True)
        self.request("bar.com", "/case1", False)

        self.reload_configuration("signed2-foo.pem", "signed-bar.pem")
        self.request("bar.com", "/case1", True)
        self.request("bar.com", "/case2", False)
        self.request("example.com", "/case2", True)
        self.request("example.com", "/case1", False)

        shutil.copy2(SSL_DIRECTORY / "signed2-bar.pem", self._ats.ssl_directory / "signed-bar.pem")
        shutil.copy2(SSL_DIRECTORY / "signed-foo.pem", self._ats.ssl_directory / "signed2-foo.pem")
        (self._ats.config_directory / "sni.yaml").touch()
        self._ats.traffic_ctl("config", "set", "proxy.config.ssl.client.cert.path", f"{self._ats.ssl_directory}/")
        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        time.sleep(0.2)

        self.request("bar.com", "/case2", True)
        self.request("bar.com", "/case1", False)
        self.request("example.com", "/case1", True)
        self.request("example.com", "/case2", False)

    def verify_access_log(self) -> None:
        """Compare non-plugin certificate-selection access logging with its gold file."""

        line_count = 8 if self._wildcard_matrix else 12
        content = wait_for_file_lines(self._ats.log_directory / "squid.log", r"^[0-9]+ ", line_count)
        gold = "proxycert2-accesslog.gold" if self._wildcard_matrix else "proxycert-accesslog.gold"
        assert_matches_gold(content, TEST_DIRECTORY / "gold" / gold)

    def run(self) -> None:
        """Start the origins and ATS, then execute the selected matrix."""

        self._first.start()
        self._second.start()
        self._ats.start()
        if self._wildcard_matrix:
            self.verify_wildcard_matrix()
        else:
            self.verify_reload_matrix()
        if not self._use_secret_plugin:
            self.verify_access_log()
