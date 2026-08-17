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
import time

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsSniHostPolicyScenario:
    """Exercise client-certificate and Host/SNI mismatch policy together."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the origin used after accepted inbound connections."""

        origin = services.origin("origin")
        for path in ("/case1", "/warnonly"):
            origin.add_response(
                {"headers": f"GET {path} HTTP/1.1\r\nHost: ignored\r\n\r\n"},
                {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure strict global Host/SNI policy with per-name exceptions."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "signer.pem")
        ats.records.update(
            {
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.host_sni_policy": 2,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: boBliTe\n"
            "    verify_client: STRICT\n"
            "    host_sni_policy: PERMISSIVE\n"
            "  - fqdn: bOb\n"
            "    verify_client: STRICT\n"
            "  - fqdn: bob.bar.com\n"
            "    verify_client: STRICT\n"
            "  - fqdn: dave.bob\n"
            "    verify_client: STRICT\n"
            "  - fqdn: noipallow.example.com\n"
            "    http2: off\n"
            "  - fqdn: ipallow_nomatch.example.com\n"
            "    ip_allow: 192.168.1.1\n",
        )
        return ats

    def request(self, sni: str, host: str, *, certificate: bool = False, path: str = "/case1") -> CommandResult:
        """Send one TLS 1.2 request with independently selected SNI and Host."""

        arguments = ["--verbose", "--tls-max", "1.2", "--insecure", "--http1.1"]
        if certificate:
            arguments.extend(("--cert", str(SSL_DIRECTORY / "signed-foo.pem"), "--key", str(SSL_DIRECTORY / "signed-foo.key")))
        arguments.extend(
            (
                "--header",
                f"Host: {host}",
                "--resolve",
                f"{sni}:{self._ats.https_port}:127.0.0.1",
                f"https://{sni}:{self._ats.https_port}{path}",
            ))
        return self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )

    def verify_matrix(self) -> None:
        """Run handshake failures, terminating mismatches, and permissive mismatches."""

        cases = (
            ("Bob", "dave", False, "/case1", "handshake"),
            ("Bob", "dave", True, "/case1", "allowed"),
            ("dave", "Bob", False, "/case1", "denied"),
            ("dave", "bob", True, "/case1", "denied"),
            ("Bob", "boB", True, "/case1", "allowed"),
            ("ellen", "Boblite", False, "/warnonly", "allowed"),
            ("ellen", "Boblite", True, "/warnonly", "allowed"),
            ("ellen", "fran", False, "/warnonly", "allowed"),
            ("ellen", "fran", True, "/warnonly", "allowed"),
            ("bob.bar.com", "bob", True, "/case1", "denied"),
            ("bob", "bob.bar.com", True, "/case1", "denied"),
            ("bob", "dave.bob", True, "/case1", "denied"),
            ("dave.bob", "bob", True, "/case1", "denied"),
            ("other.example.com", "ipallow_nomatch.example.com", False, "/case1", "denied"),
            ("other.example.com", "noipallow.example.com", False, "/case1", "allowed"),
        )
        for sni, host, certificate, path, outcome in cases:
            result = self.request(sni, host, certificate=certificate, path=path)
            if outcome == "handshake":
                assert result.returncode == 35, result.output
            else:
                assert result.returncode == 0, result.output
                if outcome == "denied":
                    assert "Access Denied" in result.output
                else:
                    assert "Access Denied" not in result.output

    def verify_diagnostics(self) -> None:
        """Verify terminating, permissive, prefix, suffix, and no-policy diagnostics."""

        diagnostics = self._ats.diags_log.read_text(errors="replace")
        for expression in (
                "SNI/hostname mismatch sni=dave host=bob action=terminate",
                "SNI/hostname mismatch sni=ellen host=Boblite action=continue",
                "SNI/hostname mismatch sni=bob.bar.com host=bob action=terminate",
                "SNI/hostname mismatch sni=bob host=bob.bar.com action=terminate",
                "SNI/hostname mismatch sni=bob host=dave.bob action=terminate",
                "SNI/hostname mismatch sni=dave.bob host=bob action=terminate",
        ):
            assert expression in diagnostics
        assert "SNI/hostname mismatch sni=ellen host=fran" not in diagnostics
        assert "SNI/hostname mismatch sni=other.example.com host=noipallow.example.com" not in diagnostics

        expected_error = "for host='bob' sni='dave', returning a 403"
        for _ in range(200):
            errors = self._ats.error_log.read_text(errors="replace") if self._ats.error_log.exists() else ""
            if expected_error in errors:
                return
            time.sleep(0.05)
        raise AssertionError(f"Expected Host/SNI mismatch error:\n{errors}")

    def run(self) -> None:
        """Start the native services and verify the complete policy matrix."""

        self._origin.start()
        self._ats.start()
        self.verify_matrix()
        self.verify_diagnostics()


def test_tls_sni_host_policy(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Host/SNI policy is case-insensitive and applies only to configured actions."""

    TlsSniHostPolicyScenario(ats_factory, services, curl).run()
