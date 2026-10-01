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
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Offer one permitted and two prohibited HTTP/2 cipher suites.

    :param ats_factory: Factory for isolated Traffic Server instances.
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
            "proxy.config.ssl.server.version.min": 2,
            "proxy.config.ssl.server.version.max": 2,
            "proxy.config.ssl.server.cipher_suite":
                ("ECDHE-RSA-AES128-GCM-SHA256:AES128-GCM-SHA256:"
                 "ECDHE-RSA-AES128-SHA256:@SECLEVEL=0"),
        })
    return ats


def run_client(cipher: str, *, send_request: bool = False, _ats: ATS) -> str:
    """Negotiate a selected TLS 1.2 cipher and return OpenSSL's output.

    :param _ats: Test-local ats configured by the test.
    :param cipher: Cipher used by this test step.
    :param send_request: Send request used by this test step.
    """

    prefix = ""
    arguments = ""
    if send_request:
        prefix = "printf 'GET / HTTP/1.1\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n' | "
        arguments = "-ign_eof "
    result = _ats.run_shell(
        f"{prefix}openssl s_client {arguments}-tls1_2 -cipher {cipher} "
        f"-alpn h2,http/1.1 -connect 127.0.0.1:{_ats.https_port}" + ("" if send_request else " </dev/null"))
    assert result.returncode == 0, result.output
    return result.output


def test_tls_h2_cipher_suite(ats_factory: ATSFactory) -> None:
    """HTTP/2 is not negotiated with prohibited TLS cipher suites.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    _ats.start()
    allowed = run_client("ECDHE-RSA-AES128-GCM-SHA256", _ats=_ats)
    assert "ALPN protocol: h2" in allowed
    for cipher in ("AES128-GCM-SHA256", "ECDHE-RSA-AES128-SHA256"):
        prohibited = run_client(cipher, send_request=True, _ats=_ats)
        assert "ALPN protocol: http/1.1" in prohibited
        assert "HTTP/1.1 404" in prohibited
