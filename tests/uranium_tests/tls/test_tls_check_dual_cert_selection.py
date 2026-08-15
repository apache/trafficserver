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


class DualCertSelectionScenario:
    """Verify ATS selects between ECDSA and RSA certificates."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Install paired certificates and prefer the ECDSA cipher."""

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
            "signer.pem",
            "signer.key",
            "server.pem",
            "server.key",
        )
        ats.copy_to_ssl(*(SSL_DIRECTORY / name for name in names))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                "  - ssl_cert_name: signed-foo-ec.pem,signed-foo.pem",
                "    ssl_key_name: signed-foo-ec.key,signed-foo.key",
                "  - ssl_cert_name: signed-san-ec.pem,signed-san.pem",
                "    ssl_key_name: signed-san-ec.key,signed-san.key",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.cipher_suite": ("ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256"),
                "proxy.config.diags.debug.tags": "ssl",
                "proxy.config.diags.debug.enabled": 1,
            })
        return ats

    @staticmethod
    def certificate_prefix(name: str) -> str:
        """Return the PEM certificate portion before its end marker."""

        content = (SSL_DIRECTORY / name).read_text()
        return content[:content.index("END CERTIFICATE-----")]

    def handshake(self, hostname: str, *, rsa_only: bool = False) -> str:
        """Perform a TLS 1.2 handshake for @a hostname."""

        cipher = " -cipher ECDHE-RSA-AES128-GCM-SHA256" if rsa_only else ""
        script = (
            f"printf 'foo\\n' | openssl s_client -tls1_2 -servername {shlex.quote(hostname)}"
            f"{cipher} -connect 127.0.0.1:{self._ats.https_port}")
        result = self._ats.run_shell(script)
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Check paired and single-algorithm SNI names."""

        self._ats.start()
        assert self.certificate_prefix("signed-foo-ec.pem") in self.handshake("foo.com")
        assert self.certificate_prefix("signed-foo.pem") in self.handshake("foo.com", rsa_only=True)

        san_ec = self.handshake("two.com")
        assert self.certificate_prefix("signed-san-ec.pem") in san_ec
        assert "CN=group.com" in san_ec

        san_rsa = self.handshake("two.com", rsa_only=True)
        assert self.certificate_prefix("signed-san.pem") in san_rsa
        assert "CN=group.com" in san_rsa

        rsa_only = self.handshake("rsa.com")
        assert self.certificate_prefix("signed-san.pem") in rsa_only
        assert "CN=group.com" in rsa_only

        ec_only = self.handshake("ec.com")
        assert self.certificate_prefix("signed-san-ec.pem") in ec_only
        assert "CN=group.com" in ec_only


def test_tls_check_dual_cert_selection(ats_factory: ATSFactory) -> None:
    """ATS selects the matching ECDSA or RSA certificate for each client."""

    DualCertSelectionScenario(ats_factory).run()
