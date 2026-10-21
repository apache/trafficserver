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

import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Start ATS with its normal TLS protocol advertisement.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    return ats_factory.create("ts", enable_tls=True)


def require_output(result: CommandResult, *expressions: str) -> None:
    """Require each observable handshake or response marker.

    :param result: Completed command result to validate.
    :param expressions: Expressions used by this test step.
    """

    for expression in expressions:
        assert expression in result.output, result.output


def run_openssl_cases(*, _ats: ATS) -> None:
    """Exercise invalid, HTTP/1.1, and absent ALPN offers.

    :param _ats: Test-local ats configured by the test.
    """

    port = _ats.https_port
    invalid = _ats.run_shell(f"timeout 5 openssl s_client -alpn banana -connect 127.0.0.1:{port} </dev/null")
    assert invalid.returncode in (0, 1, 124), invalid.output
    require_output(invalid, "No ALPN negotiated")

    http1 = _ats.run_shell(
        f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client -ign_eof -alpn http/1.1 -connect 127.0.0.1:{port}")
    assert http1.returncode == 0, http1.output
    require_output(http1, "ALPN protocol: http/1.1", "HTTP/1.1 400 Host Header Required")

    absent = _ats.run_shell(f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client -ign_eof -connect 127.0.0.1:{port}")
    assert absent.returncode == 0, absent.output
    require_output(absent, "No ALPN negotiated", "HTTP/1.1 400 Host Header Required")


def run_http2_case(*, _ats: ATS, _curl: Curl) -> None:
    """Verify curl negotiates h2 and receives an ordinary response.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    if not _curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    result = _curl.run(f"--insecure --http2 --verbose --output /dev/null 'https://127.0.0.1:{_ats.https_port}/'",)
    assert result.returncode == 0, result.output
    assert "ALPN: server accepted h2" in result.output
    assert "HTTP/2 404" in result.output


def test_tls_bad_alpn(ats_factory: ATSFactory, curl: Curl) -> None:
    """Unsupported ALPN is declined while supported protocols still work.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """

    if shutil.which("openssl") is None:
        pytest.skip("OpenSSL is required")
    _ats = configure_ats(ats_factory)

    _ats.start()
    run_openssl_cases(_ats=_ats)
    run_http2_case(_ats=_ats, _curl=curl)
