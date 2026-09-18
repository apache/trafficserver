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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsHooksVerifyScenario:
    """Exercise enforced and permissive SERVER_VERIFY_HOOK outcomes."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create an HTTPS origin with a certificate that needs hook handling."""

        origin = services.origin("server", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the verification plugin and configure three SNI policies."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
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
                "proxy.config.diags.debug.tags": "ssl_verify_test",
                "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
                "proxy.config.ssl.client.verify.server.properties": "NONE",
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        for hostname in ("foo.com", "bar.com", "random.com"):
            ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ https://127.0.0.1:{self._origin.https_port}")
        ats.write_config_file("sni.yaml", "sni:\n- fqdn: bar.com\n  verify_server_policy: PERMISSIVE\n")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_verify_test.so")
        ats.plugin_config.add_line("ssl_verify_test.so -count=2 -bad=random.com -bad=bar.com")
        return ats

    def request(self, hostname: str) -> str:
        """Send one TLS request using @a hostname as SNI and Host."""

        result = self._curl.run_for(
            self._ats,
            (f"--resolve '{hostname}:{self._ats.https_port}:127.0.0.1' --insecure "
             f"'https://{hostname}:{self._ats.https_port}'"),
        )
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Verify hook decisions and both callback invocations for each SNI."""

        self._origin.start()
        self._ats.start()
        assert "Could Not Connect" not in self.request("foo.com")
        assert "Could Not Connect" in self.request("random.com")
        assert "Could Not Connect" not in self.request("bar.com")

        diags = self._ats.diags_log.read_text(errors="replace")
        assert "Action=Terminate SNI=random.com" in diags
        assert "Action=Continue SNI=bar.com" in diags
        assert "SNI=foo.com" not in diags

        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        for hostname, outcome in (("foo.com", "good HS"), ("random.com", "error HS"), ("bar.com", "error HS")):
            for callback in (0, 1):
                expression = rf"Server verify callback {callback} [\da-fx]+? - event is good SNI={hostname} {outcome}"
                assert re.search(expression, traffic_out), traffic_out
        assert "Server verify callback SNI APIs match=true" in traffic_out


def test_tls_hooks_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SERVER_VERIFY_HOOK decisions honor the configured SNI policy."""

    TlsHooksVerifyScenario(ats_factory, services, curl).run()
