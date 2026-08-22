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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsSniYamlReloadScenario:
    """Verify that a failed sni.yaml reload leaves the active policy intact."""

    _hostname = "example.com"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("SNI client-certificate coverage requires a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the reusable empty-response origin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: {self._hostname}\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def sni_document(self, *, valid: bool) -> str:
        """Render the valid initial or intentionally invalid replacement policy."""

        suffix = "foo" if valid else "notexist"
        http2 = "off" if valid else "on"
        return (
            "sni:\n"
            f"- fqdn: {self._hostname}\n"
            f"  http2: {http2}\n"
            f"  client_cert: {self._ats.ssl_directory}/signed-{suffix}.pem\n"
            f"  client_key: {self._ats.ssl_directory}/signed-{suffix}.key\n"
            "  verify_client: STRICT\n")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the initial valid SNI policy and trust store."""

        ats = ats_factory.create("ts", enable_tls=True, disable_log_checks=True)
        self._ats = ats
        ats.add_default_ssl_files()
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "signed-foo.pem",
            TEST_DIRECTORY / "ssl" / "signed-foo.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.records.update(
            {
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl|http",
                "proxy.config.diags.output.debug": "L",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.write_config_file("sni.yaml", self.sni_document(valid=True))
        return ats

    def request(self, certificate: str, key: str) -> str:
        """Send one TLS 1.2 request with a trusted client certificate."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--tls-max 1.2 --silent --verbose --insecure --cert '{str(TEST_DIRECTORY / 'ssl' / certificate)}' "
                f"--key '{str(TEST_DIRECTORY / 'ssl' / key)}' --resolve "
                f"'{self._hostname}:{self._ats.https_port}:127.0.0.1' "
                f"'https://{self._hostname}:{self._ats.https_port}'"),
        )
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Reject the bad reload and prove the previous HTTP/1.1 policy remains."""

        self._origin.start()
        self._ats.start()
        initial = self.request("signed-foo.pem", "signed-foo.key")
        assert "Could Not Connect" not in initial
        assert self._hostname in initial

        (self._ats.config_directory / "sni.yaml").write_text(self.sni_document(valid=False))
        reload_result = self._ats.traffic_ctl(
            "config", "reload", "-m", "-t", "invalid-sni-reload", "-w", "1", "-r", "0.5", "-T", "30s")
        assert reload_result.returncode == 2, reload_result.output

        final = self.request("signed-bar.pem", "signed-bar.key")
        assert "GET / HTTP/2" not in final


def test_tls_sni_yaml_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An invalid sni.yaml reload rolls back without changing active policy."""

    TlsSniYamlReloadScenario(ats_factory, services, curl).run()
