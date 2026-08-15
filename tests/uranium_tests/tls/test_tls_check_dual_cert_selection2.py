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

from tools.uranium.services import ATS, ATSFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


class CombinedDualCertSelectionScenario:
    """Select dual certificates whose private keys are in the PEM files."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure paired combined and separate certificate files."""

        ats = ats_factory.create("ts", enable_tls=True)
        names = (
            "signed-foo.pem",
            "signed-foo.key",
            "signed-foo-ec.pem",
            "signed-foo-ec.key",
            "signed-san.pem",
            "signed-san.key",
            "signed-san-ec.pem",
            "signed-san-ec.key",
            "combined-ec.pem",
            "combined.pem",
            "signer.pem",
            "signer.key",
        )
        ats.copy_to_ssl(*(SSL_DIRECTORY / name for name in names))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                "  - ssl_cert_name: combined-ec.pem,combined.pem",
                "  - ssl_cert_name: signed-foo-ec.pem,signed-foo.pem",
                '  - dest_ip: "*"',
                "    ssl_cert_name: signed-san-ec.pem,signed-san.pem",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": "/tmp",
                "proxy.config.ssl.server.cipher_suite": ("ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256"),
            })
        return ats

    @staticmethod
    def certificate_prefix(name: str) -> str:
        """Return the certificate portion of one PEM file."""

        content = (SSL_DIRECTORY / name).read_text()
        return content[:content.index("END CERTIFICATE-----")]

    def handshake(self, hostname: str, *, rsa_only: bool = False) -> str:
        """Perform a TLS 1.2 handshake for @a hostname."""

        cipher = " -cipher ECDHE-RSA-AES128-GCM-SHA256" if rsa_only else ""
        result = self._ats.run_shell(
            f"printf 'foo\\n' | openssl s_client -tls1_2 -servername {shlex.quote(hostname)}"
            f"{cipher} -connect 127.0.0.1:{self._ats.https_port}")
        assert result.returncode == 0, result.output
        return result.output

    def assert_certificate(self, hostname: str, filename: str, *, rsa_only: bool = False, common_name: str) -> None:
        """Require one expected certificate for a handshake."""

        output = self.handshake(hostname, rsa_only=rsa_only)
        assert self.certificate_prefix(filename) in output
        assert f"CN={common_name}" in output

    def run(self) -> None:
        """Check separate-key, SAN, and combined PEM certificate pairs."""

        self._ats.start()
        self.assert_certificate("foo.com", "signed-foo-ec.pem", common_name="foo.com")
        self.assert_certificate("foo.com", "signed-foo.pem", rsa_only=True, common_name="foo.com")
        self.assert_certificate("two.com", "signed-san-ec.pem", common_name="group.com")
        self.assert_certificate("two.com", "signed-san.pem", rsa_only=True, common_name="group.com")
        self.assert_certificate("rsa.com", "signed-san.pem", common_name="group.com")
        self.assert_certificate("ec.com", "signed-san-ec.pem", common_name="group.com")
        self.assert_certificate("combined.com", "combined-ec.pem", common_name="combined.com")
        self.assert_certificate("combined.com", "combined.pem", rsa_only=True, common_name="combined.com")


def test_tls_check_dual_cert_selection2(ats_factory: ATSFactory) -> None:
    """Combined PEM files work even when the configured key path is invalid."""

    CombinedDualCertSelectionScenario(ats_factory).run()
