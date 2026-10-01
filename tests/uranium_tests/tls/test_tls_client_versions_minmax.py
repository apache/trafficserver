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


def configure_ats(ats_factory: ATSFactory, openssl_version: str) -> ATS:
    """Configure global TLS 1.2 and an SNI-specific TLS 1.0–1.1 range.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param openssl_version: Openssl version used by this test step.
    """

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


def request(hostname: str, version: str, expected_code: int | None, *, _ats: ATS, _curl: Curl) -> int:
    """Offer exactly one TLS version and return curl's status.

    :param hostname: SNI hostname and request authority.
    :param version: Maximum TLS version to offer.
    :param expected_code: Expected curl exit status, or ``None`` to accept
        any status.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    protocol_option = {"1.0": "--tlsv1", "1.1": "--tlsv1.1", "1.2": "--tlsv1.2"}[version]
    result = _curl.run_for(
        _ats,
        f"--verbose --ciphers DEFAULT@SECLEVEL=0 --tls-max {version} {protocol_option} "
        f"--resolve {hostname}:{_ats.https_port}:127.0.0.1 --insecure "
        f"https://{hostname}:{_ats.https_port}",
    )
    if expected_code is not None:
        assert result.returncode == expected_code, result.output
    return result.returncode


def test_tls_client_versions_minmax(ats_factory: ATSFactory, curl: Curl) -> None:
    """TLS min/max range records take precedence over boolean settings.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("TLS version negotiation requires a TCP listener")
    openssl = subprocess.check_output(("openssl", "version"), text=True)
    version = re.search(r"\d+(?:\.\d+)+", openssl)
    if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    curl_help = subprocess.check_output(("curl", "--help", "all"), text=True)
    _supports_tls_1_0 = "--tlsv1" in curl_help
    _supports_tls_1_1 = "--tlsv1.1" in curl_help
    _ats = configure_ats(ats_factory, openssl)

    _ats.start()
    request("foo.com", "1.2", 35, _ats=_ats, _curl=curl)
    if _supports_tls_1_0:
        if request("foo.com", "1.0", None, _ats=_ats, _curl=curl) != 0:
            pytest.skip("the runtime TLS stack cannot complete a TLS 1.0 handshake")
    if _supports_tls_1_1:
        request("foo.com", "1.1", 0, _ats=_ats, _curl=curl)
    if _supports_tls_1_0:
        request("bar.com", "1.0", 35, _ats=_ats, _curl=curl)
    request("bar.com", "1.2", 0, _ats=_ats, _curl=curl)
