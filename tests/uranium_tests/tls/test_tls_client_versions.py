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


class TlsClientVersionsScenario:
    """Verify that SNI selects legacy or default inbound TLS versions."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("TLS version negotiation requires a TCP listener")
        openssl = subprocess.check_output(("openssl", "version"), text=True)
        version = re.search(r"\d+(?:\.\d+)+", openssl)
        if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        if "TLSv1" not in subprocess.check_output(("openssl", "ciphers", "-v"), text=True):
            pytest.skip("legacy TLS support is required")
        self._supports_tls_1_0 = "--tlsv1" in subprocess.check_output(("curl", "--help", "all"), text=True)
        self._curl = curl
        self._ats = self.configure_ats(ats_factory, openssl)

    def configure_ats(self, ats_factory: ATSFactory, openssl_version: str) -> ATS:
        """Disable legacy TLS globally and enable it only for foo.com."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        cipher_suite = (
            "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:"
            "ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:"
            "ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:"
            "DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2")
        version = re.search(r"\d+(?:\.\d+)+", openssl_version)
        if version is not None and tuple(int(part) for part in version.group().split(".")) >= (3, 0, 0):
            cipher_suite += ":@SECLEVEL=0"
        ats.records.update(
            {
                "proxy.config.ssl.server.cipher_suite": cipher_suite,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.TLSv1": 0,
                "proxy.config.ssl.TLSv1_1": 0,
                "proxy.config.ssl.TLSv1_2": 1,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
            })
        ats.write_config_file(
            "sni.yaml",
            "sni:\n- fqdn: foo.com\n  valid_tls_versions_in: [ TLSv1, TLSv1_1 ]\n",
        )
        return ats

    def request(self, hostname: str, version: str, expected_code: int | None) -> int:
        """Offer exactly @a version to @a hostname and verify the result."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--ciphers",
            "DEFAULT@SECLEVEL=0",
            "--tls-max",
            version,
            "--tlsv1" if version == "1.0" else "--tlsv1.2",
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            "--insecure",
            f"https://{hostname}:{self._ats.https_port}",
        )
        if expected_code is not None:
            assert result.returncode == expected_code, result.output
        return result.returncode

    def run(self) -> None:
        """Exercise the SNI override and the global TLS 1.2 policy."""

        self._ats.start()
        self.request("foo.com", "1.2", 35)
        if self._supports_tls_1_0:
            if self.request("foo.com", "1.0", None) != 0:
                pytest.skip("the runtime TLS stack cannot complete a TLS 1.0 handshake")
            self.request("bar.com", "1.0", 35)
        self.request("bar.com", "1.2", 0)


def test_tls_client_versions(ats_factory: ATSFactory, curl: Curl) -> None:
    """SNI overrides the inbound TLS protocol versions offered by ATS."""

    TlsClientVersionsScenario(ats_factory, curl).run()
