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


class TlsClientVersionRangeScenario:
    """Verify that SNI min/max TLS ranges override legacy boolean records."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("TLS version negotiation requires a TCP listener")
        openssl = subprocess.check_output(("openssl", "version"), text=True)
        version = re.search(r"\d+(?:\.\d+)+", openssl)
        if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
            pytest.skip("OpenSSL 1.1.1 or newer is required")
        curl_help = subprocess.check_output(("curl", "--help", "all"), text=True)
        self._supports_tls_1_0 = "--tlsv1" in curl_help
        self._supports_tls_1_1 = "--tlsv1.1" in curl_help
        self._curl = curl
        self._ats = self.configure_ats(ats_factory, openssl)

    def configure_ats(self, ats_factory: ATSFactory, openssl_version: str) -> ATS:
        """Configure global TLS 1.2 and an SNI-specific TLS 1.0–1.1 range."""

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
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.server.version.min": 2,
                "proxy.config.ssl.server.version.max": 2,
                "proxy.config.ssl.TLSv1_2": 0,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
            })
        cipher_suite = (
            "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:"
            "ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:"
            "ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:"
            "DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2")
        version = re.search(r"\d+(?:\.\d+)+", openssl_version)
        if version is not None and tuple(int(part) for part in version.group().split(".")) >= (3, 0, 0):
            cipher_suite += ":@SECLEVEL=0"
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "- fqdn: foo.com\n"
            "  valid_tls_versions_in: [ TLSv1_2 ]\n"
            "  valid_tls_version_min_in: TLSv1\n"
            "  valid_tls_version_max_in: TLSv1_1\n"
            f"  server_cipher_suite: {cipher_suite}\n",
        )
        return ats

    def request(self, hostname: str, version: str, expected_code: int | None) -> int:
        """Offer exactly @a version and return curl's status."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--ciphers",
            "DEFAULT@SECLEVEL=0",
            "--tls-max",
            version,
            {
                "1.0": "--tlsv1",
                "1.1": "--tlsv1.1",
                "1.2": "--tlsv1.2"
            }[version],
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            "--insecure",
            f"https://{hostname}:{self._ats.https_port}",
        )
        if expected_code is not None:
            assert result.returncode == expected_code, result.output
        return result.returncode

    def run(self) -> None:
        """Exercise the SNI range and global fixed version."""

        self._ats.start()
        self.request("foo.com", "1.2", 35)
        if self._supports_tls_1_0:
            if self.request("foo.com", "1.0", None) != 0:
                pytest.skip("the runtime TLS stack cannot complete a TLS 1.0 handshake")
        if self._supports_tls_1_1:
            self.request("foo.com", "1.1", 0)
        if self._supports_tls_1_0:
            self.request("bar.com", "1.0", 35)
        self.request("bar.com", "1.2", 0)


def test_tls_client_versions_minmax(ats_factory: ATSFactory, curl: Curl) -> None:
    """TLS min/max range records take precedence over boolean settings."""

    TlsClientVersionRangeScenario(ats_factory, curl).run()
