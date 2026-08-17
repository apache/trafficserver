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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class DefaultTlsSecretUpdateScenario:
    """Refresh the default no-SNI SSL context through the secret update API."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the clear-text origin reached after the inbound TLS handshake."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure two wildcard certificates and the secret-loader plugin."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            SSL_DIRECTORY / "signed-bar.pem",
            SSL_DIRECTORY / "signed2-bar.pem",
            SSL_DIRECTORY / "signed-bar.key",
            SSL_DIRECTORY / "signed-foo.pem",
            SSL_DIRECTORY / "signed2-foo.pem",
            SSL_DIRECTORY / "signed-foo.key",
        )
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
        ats.plugin_config.add_line("ssl_secret_load_test.so")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl_secret_load_test",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory.parent),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory.parent),
                "proxy.config.ssl.server.multicert.concurrency": 1,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        ats.set_ssl_multicert_yaml(
            {
                "ssl_multicert":
                    [
                        {
                            "dest_ip": "*",
                            "ssl_cert_name": "signed-bar.pem",
                            "ssl_key_name": "signed-bar.key"
                        },
                        {
                            "dest_ip": "*",
                            "ssl_cert_name": "signed-foo.pem",
                            "ssl_key_name": "signed-foo.key"
                        },
                    ]
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def certificate_output(self) -> str:
        """Connect without SNI and return curl's certificate diagnostics."""

        result = self._curl.run_for(
            self._ats,
            (f"--insecure --verbose --http1.1 --header 'Host: doesnotmatter' "
             f"'https://127.0.0.1:{self._ats.https_port}/'"),
        )
        assert result.returncode == 0, result.output
        return result.output

    def await_secret_update(self, filename: str) -> None:
        """Wait until the test plugin reports refreshing one certificate secret."""

        for _ in range(120):
            output = self._ats.traffic_out.read_text(errors="replace") if self._ats.traffic_out.exists() else ""
            if any("updated cert for secret" in line and filename in line for line in output.splitlines()):
                return
            time.sleep(0.1)
        raise AssertionError(f"The secret-loader did not refresh {filename}:\n{output}")

    @staticmethod
    def assert_certificate(output: str, common_name: str, issuer: str) -> None:
        """Require the selected subject and issuer in curl's TLS diagnostics."""

        assert f"CN={common_name}" in output or f"CN = {common_name}" in output
        assert f"CN={issuer}" in output or f"CN = {issuer}" in output

    def run(self) -> None:
        """Update a shadowed wildcard and then the active default certificate."""

        self._origin.start()
        self._ats.start()
        self._ats.ssl_multicert_config.path.touch()
        reload_result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert reload_result.returncode == 0, reload_result.output

        initial = self.certificate_output()
        self.assert_certificate(initial, "bar.com", "signer.yahoo.com")
        assert "signer2.yahoo.com" not in initial

        time.sleep(2)
        shutil.copy2(SSL_DIRECTORY / "signed2-foo.pem", self._ats.ssl_directory / "signed-foo.pem")
        (self._ats.ssl_directory / "signed-foo.pem").touch()
        self.await_secret_update("signed-foo.pem")
        unchanged = self.certificate_output()
        self.assert_certificate(unchanged, "bar.com", "signer.yahoo.com")
        assert "foo.com" not in unchanged

        shutil.copy2(SSL_DIRECTORY / "signed2-bar.pem", self._ats.ssl_directory / "signed-bar.pem")
        (self._ats.ssl_directory / "signed-bar.pem").touch()
        self.await_secret_update("signed-bar.pem")
        updated = self.certificate_output()
        self.assert_certificate(updated, "bar.com", "signer2.yahoo.com")


def test_tls_secret_update_default(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Updating a shadowed certificate cannot replace the default SSL context."""

    DefaultTlsSecretUpdateScenario(ats_factory, services, curl).run()
