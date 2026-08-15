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
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl

TEST_DIRECTORY = Path(__file__).parent


def openssl_at_least(required: tuple[int, ...]) -> bool:
    """Return whether the runtime OpenSSL version is at least @a required."""

    output = subprocess.check_output(("openssl", "version"), text=True)
    match = re.search(r"\d+(?:\.\d+)+", output)
    return match is not None and tuple(int(part) for part in match.group().split(".")) >= required


class TlsSniGroupsScenario:
    """Verify per-SNI TLS group selection and invalid-group rejection."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("TLS SNI handshake coverage requires a TCP listener")
        if not openssl_at_least((1, 1, 1)):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        self._curl = curl
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS group and cipher policy for three SNI names."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_sni",
        })
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "- fqdn: aaa.com\n"
            "  server_groups_list: X25519MLKEM768\n"
            "  valid_tls_versions_in: [ TLSv1_3 ]\n"
            "  server_TLSv1_3_cipher_suites: TLS_AES_256_GCM_SHA384\n"
            "- fqdn: bbb.com\n"
            "  server_groups_list: x25519\n"
            "  valid_tls_versions_in: [ TLSv1_2 ]\n"
            "  server_cipher_suite: ECDHE-RSA-AES256-GCM-SHA384\n"
            "- fqdn: ccc.com\n"
            "  server_groups_list: ABC123\n"
            "  valid_tls_versions_in: [ TLSv1_2 ]\n"
            "  server_cipher_suite: ECDHE-RSA-AES256-GCM-SHA384\n",
        )
        return ats

    def request(self, hostname: str, *cipher_options: str) -> str:
        """Run curl with @a hostname and selected cipher options."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            *cipher_options,
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            "--insecure",
            f"https://{hostname}:{self._ats.https_port}",
        )
        if hostname == "ccc.com":
            assert result.returncode == 35, result.output
        else:
            assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Exercise a valid TLS 1.2 group, an invalid group, and PQ support."""

        self._ats.start()
        output = self.request("bbb.com", "--ciphers", "ECDHE-RSA-AES256-GCM-SHA384")
        assert "SSL connection using TLSv1.2 / ECDHE-RSA-AES256-GCM-SHA384 / x25519" in output
        self.request("ccc.com", "--ciphers", "ECDHE-RSA-AES256-GCM-SHA384")

        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "Setting groups list from server_groups_list to x25519" in traffic_out
        assert "ERROR: Invalid server_groups_list: ABC123" in self._ats.diags_log.read_text(errors="replace")

        if openssl_at_least((3, 5, 0)):
            output = self.request("aaa.com", "--tls13-ciphers", "TLS_AES_256_GCM_SHA384")
            assert "SSL connection using TLSv1.3 / TLS_AES_256_GCM_SHA384 / X25519MLKEM768" in output
            traffic_out = self._ats.traffic_out.read_text(errors="replace")
            assert "Setting groups list from server_groups_list to X25519MLKEM768" in traffic_out


def test_tls_sni_groups(ats_factory: ATSFactory, curl: Curl) -> None:
    """SNI policy selects supported TLS groups and rejects invalid ones."""

    TlsSniGroupsScenario(ats_factory, curl).run()
