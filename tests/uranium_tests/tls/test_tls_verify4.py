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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


class ReloadableTlsVerifyScenario:
    """Change outbound certificate verification policy at runtime."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create an HTTPS origin whose certificate is not trusted by ATS."""

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: random.example\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure enforced verification against an unrelated CA."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.copy_to_ssl(SSL_DIRECTORY / "signer.pem")
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._origin.https_port}")
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
                "proxy.config.ssl.client.verify.server.properties": "ALL",
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
            })
        return ats

    def request(self, host: str) -> str:
        """Send one request with @a host through the ATS TLS listener."""

        result = self._curl.run_for(
            self._ats,
            f"--insecure --header 'Host: {host}' 'https://127.0.0.1:{self._ats.https_port}/'",
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def set_policy(self, policy: str) -> None:
        """Set the reloadable outbound verification policy."""

        result = self._ats.traffic_ctl("config", "set", "proxy.config.ssl.client.verify.server.policy", policy)
        assert result.returncode == 0, result.output
        time.sleep(0.2)

    def run(self) -> None:
        """Verify enforced, permissive, and restored enforced behavior."""

        self._origin.start()
        self._ats.start()
        assert "Could Not Connect" in self.request("random2.com")
        self.set_policy("PERMISSIVE")
        assert "Could Not Connect" not in self.request("random3.com")
        self.set_policy("ENFORCED")
        assert "Could Not Connect" in self.request("random4.com")

        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "Core server certificate verification failed for (random3.com). Action=Continue" in diagnostics
        assert "Core server certificate verification failed for (random2.com). Action=Terminate" in diagnostics


def test_tls_verify4(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Outbound TLS verification policy changes take effect without restart."""

    ReloadableTlsVerifyScenario(ats_factory, services, curl).run()
