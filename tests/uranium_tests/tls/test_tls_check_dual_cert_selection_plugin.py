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
import shutil
import time

from tools.uranium.services import ATS, ATSFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


class PluginDualCertSelectionScenario:
    """Select and refresh paired ECDSA and RSA hook-provided certificates."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure paired certificates through ssl_secret_load_test."""

        ats = ats_factory.create("ts", enable_tls=True)
        names = (
            "signed-foo.pem",
            "signed2-foo.pem",
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
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
        ats.plugin_config.add_line("ssl_secret_load_test.so")
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
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory.parent),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory.parent),
                "proxy.config.diags.debug.tags": "ssl_secret_load_test",
                "proxy.config.diags.debug.enabled": 1,
            })
        return ats

    @staticmethod
    def certificate_prefix(name: str) -> str:
        """Return the PEM certificate portion before its end marker."""

        content = (SSL_DIRECTORY / name).read_text()
        return content[:content.index("END CERTIFICATE-----")]

    def handshake(self, hostname: str, *, rsa_only: bool = False, ca_file: str = "signer.pem") -> str:
        """Perform one OpenSSL handshake with optional RSA-only signatures."""

        sigalgs = " -sigalgs RSA-PSS+SHA256" if rsa_only else ""
        script = (
            f"printf 'foo\\n' | openssl s_client -CAfile {shlex.quote(str(SSL_DIRECTORY / ca_file))} "
            f"-servername {shlex.quote(hostname)}{sigalgs} -connect 127.0.0.1:{self._ats.https_port}")
        result = self._ats.run_shell(script)
        assert result.returncode == 0, result.output
        return result.output

    def verify_initial_selection(self) -> None:
        """Check algorithm and SAN selection before refreshing a secret."""

        assert self.certificate_prefix("signed-foo-ec.pem") in self.handshake("foo.com")
        assert self.certificate_prefix("signed-foo.pem") in self.handshake("foo.com", rsa_only=True)

        san_ec = self.handshake("one.com")
        assert self.certificate_prefix("signed-san-ec.pem") in san_ec and "CN=group.com" in san_ec
        san_rsa = self.handshake("one.com", rsa_only=True)
        assert self.certificate_prefix("signed-san.pem") in san_rsa and "CN=group.com" in san_rsa
        rsa_only = self.handshake("rsa.com")
        assert self.certificate_prefix("signed-san.pem") in rsa_only and "CN=group.com" in rsa_only
        ec_only = self.handshake("ec.com")
        assert self.certificate_prefix("signed-san-ec.pem") in ec_only and "CN=group.com" in ec_only

    def refresh_rsa_certificate(self) -> None:
        """Replace the watched RSA certificate and await the plugin poll."""

        time.sleep(1.1)
        live = self._ats.ssl_directory / "signed-foo.pem"
        shutil.copyfile(SSL_DIRECTORY / "signed2-foo.pem", live)
        live.touch()
        time.sleep(4)

    def run(self) -> None:
        """Verify algorithm selection and an isolated RSA secret refresh."""

        self._ats.start()
        self.verify_initial_selection()
        self.refresh_rsa_certificate()

        old_ca = self.handshake("foo.com", rsa_only=True)
        assert self.certificate_prefix("signed2-foo.pem") in old_ca
        assert "unable to verify the first certificate" in old_ca
        new_ca = self.handshake("foo.com", rsa_only=True, ca_file="signer2.pem")
        assert self.certificate_prefix("signed2-foo.pem") in new_ca
        assert "unable to verify the first certificate" not in new_ca
        unchanged_ec = self.handshake("foo.com")
        assert self.certificate_prefix("signed-foo-ec.pem") in unchanged_ec
        assert "unable to verify the first certificate" not in unchanged_ec


def test_tls_check_dual_cert_selection_plugin(ats_factory: ATSFactory) -> None:
    """The secret hook preserves dual-certificate selection across refresh."""

    PluginDualCertSelectionScenario(ats_factory).run()
