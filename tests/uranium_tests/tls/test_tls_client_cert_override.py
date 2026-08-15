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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsClientCertOverrideScenario:
    """Select outbound client certificates with conf_remap."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        use_secret_plugin: bool = False,
    ) -> None:
        self._curl = curl
        self._use_secret_plugin = use_secret_plugin
        self._server1 = self.configure_origin(
            services,
            "server",
            "signer.pem",
            "signed-foo.pem",
            "signed-foo.key",
        )
        self._server2 = self.configure_origin(
            services,
            "server2",
            "signer2.pem",
            "signed2-bar.pem",
            "signed-bar.key",
        )
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(
        self,
        services: ServiceFactory,
        name: str,
        client_ca: str,
        certificate: str,
        key: str,
    ) -> OriginServer:
        """Create an HTTPS origin that requires a client certificate."""

        origin = services.origin(
            name,
            ssl=True,
            clientcert=SSL_DIRECTORY / certificate,
            clientkey=SSL_DIRECTORY / key,
            options={
                "--clientCA": SSL_DIRECTORY / client_ca,
                "--clientverify": "",
            },
        )
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def remap_with_certificate(self, ats: ATS, path: str, origin: OriginServer, certificate: str, key: str) -> str:
        """Build one client-certificate remap rule."""

        return (
            f"map {path} https://127.0.0.1:{origin.https_port}/ "
            "@plugin=conf_remap.so "
            f"@pparam=proxy.config.ssl.client.cert.filename={certificate} "
            f"@pparam=proxy.config.ssl.client.private_key.filename={key}")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure matching and mismatched client-certificate routes."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("conf_remap.so"):
            pytest.skip("conf_remap.so is not installed")
        ats.copy_to_ssl(
            SSL_DIRECTORY / "server.pem",
            SSL_DIRECTORY / "server.key",
            SSL_DIRECTORY / "signed-foo.pem",
            SSL_DIRECTORY / "signed-foo.key",
            SSL_DIRECTORY / "signed2-foo.pem",
            SSL_DIRECTORY / "signed-bar.pem",
            SSL_DIRECTORY / "signed2-bar.pem",
            SSL_DIRECTORY / "signed-bar.key",
        )
        client_directory = ats.ssl_directory.parent if self._use_secret_plugin else ats.ssl_directory
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
                "proxy.config.ssl.client.cert.path": str(client_directory),
                "proxy.config.ssl.client.cert.filename": "signed-foo.pem",
                "proxy.config.ssl.client.private_key.path": str(client_directory),
                "proxy.config.ssl.client.private_key.filename": "signed-foo.key",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        if self._use_secret_plugin:
            ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
            ats.plugin_config.add_line("ssl_secret_load_test.so")
            ats.write_config_file("sni.yaml", "sni:\n  - fqdn: random\n    verify_server_properties: NONE\n")
        ats.remap_config.add_lines(
            (
                self.remap_with_certificate(ats, "/case1", self._server1, "signed-foo.pem", "signed-foo.key"),
                self.remap_with_certificate(ats, "/badcase1", self._server1, "signed2-foo.pem", "signed-foo.key"),
                self.remap_with_certificate(ats, "/case2", self._server2, "signed2-foo.pem", "signed-foo.key"),
                self.remap_with_certificate(ats, "/badcase2", self._server2, "signed-foo.pem", "signed-foo.key"),
            ))
        return ats

    def request(self, path: str, host: str) -> str:
        """Request one client-certificate selection case."""

        result = self._curl.get(self._ats, path, headers={"Host": host})
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Verify matching certificates succeed and mismatched CAs fail."""

        self._server1.start()
        self._server2.start()
        self._ats.start()
        assert "Could Not Connect" not in self.request("/case1", "example.com")
        assert "Could Not Connect" in self.request("/badcase1", "example.com")
        assert "Could Not Connect" not in self.request("/case2", "bar.com")
        assert "Could Not Connect" in self.request("/badcase2", "bar.com")
        if self._use_secret_plugin:
            self.verify_secret_updates()

    def verify_secret_updates(self) -> None:
        """Verify reload-triggered and polled in-place client-certificate changes."""

        shutil.copy2(SSL_DIRECTORY / "signed-foo.pem", self._ats.ssl_directory / "signed2-foo.pem")
        (self._ats.config_directory / "sni.yaml").touch()
        result = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.ssl.client.cert.path",
            str(self._ats.ssl_directory.parent),
        )
        assert result.returncode == 0, result.output
        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in self.request("/badcase1", "foo.com")

        shutil.copy2(SSL_DIRECTORY / "signed2-foo.pem", self._ats.ssl_directory / "signed-foo.pem")
        (self._ats.ssl_directory / "signed-foo.pem").touch()
        (self._ats.ssl_directory / "signed-foo.key").touch()
        time.sleep(4)
        assert "Could Not Connect" in self.request("/case1", "example.com")
        assert "Could Not Connect" not in self.request("/badcase1", "example.com")


def test_tls_client_cert_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """conf_remap selects an outbound client certificate per mapping."""

    TlsClientCertOverrideScenario(ats_factory, services, curl).run()
