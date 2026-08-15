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
import re
import ssl

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsHooksClientVerifyScenario:
    """Exercise CLIENT_VERIFY_HOOK with good, mismatched, and untrusted certs."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        version = re.search(r"\d+(?:\.\d+)+", ssl.OPENSSL_VERSION)
        if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        if curl.uses_uds:
            pytest.skip("client certificate verification requires a TCP TLS listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the HTTPS origin used after client authentication."""

        origin = services.origin("server", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure strict client verification and load the hook plugin."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "server.pem",
            TEST_DIRECTORY / "ssl" / "server.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl_client_verify_test",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        for hostname in ("foo.com", "bar.com", "random.com"):
            ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ https://127.0.0.1:{self._origin.https_port}")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n- fqdn: bar.com\n  verify_client: STRICT\n- fqdn: foo.com\n  verify_client: STRICT\n",
        )
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_client_verify_test.so")
        ats.plugin_config.add_line("ssl_client_verify_test.so -count=2 -good=foo.com")
        return ats

    def request(self, certificate: str, key: str, expected_code: int) -> str:
        """Send one TLS 1.2 request with the selected client certificate."""

        result = self._curl.run_for(
            self._ats,
            "--tls-max",
            "1.2",
            "--insecure",
            "--cert",
            str(TEST_DIRECTORY / "ssl" / certificate),
            "--key",
            str(TEST_DIRECTORY / "ssl" / key),
            "--resolve",
            f"foo.com:{self._ats.https_port}:127.0.0.1",
            f"https://foo.com:{self._ats.https_port}/case1",
        )
        assert result.returncode == expected_code, result.output
        return result.output

    def run(self) -> None:
        """Verify accepted and rejected client certificates and hook callbacks."""

        self._origin.start()
        self._ats.start()
        assert "Could Not Connect" not in self.request("signed-foo.pem", "signed-foo.key", 0)
        assert "error" in self.request("signed-bar.pem", "signed-bar.key", 35).lower()
        assert "error" in self.request("server.pem", "server.key", 35).lower()
        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        for outcome in ("good HS", "error HS"):
            for callback in (0, 1):
                expression = rf"Client verify callback {callback} [\da-fx]+? - event is good {outcome}"
                assert re.search(expression, traffic_out), traffic_out


def test_tls_hooks_client_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """CLIENT_VERIFY_HOOK consistently handles valid and invalid certificates."""

    TlsHooksClientVerifyScenario(ats_factory, services, curl).run()
