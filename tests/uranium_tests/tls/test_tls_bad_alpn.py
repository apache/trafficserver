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


class AlpnScenario:
    """Offer invalid, absent, HTTP/1.1, and HTTP/2 ALPN values."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self._curl = curl
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Start ATS with its normal TLS protocol advertisement."""

        return ats_factory.create("ts", enable_tls=True)

    @staticmethod
    def require_output(result: CommandResult, *expressions: str) -> None:
        """Require each observable handshake or response marker."""

        for expression in expressions:
            assert expression in result.output, result.output

    def run_openssl_cases(self) -> None:
        """Exercise invalid, HTTP/1.1, and absent ALPN offers."""

        port = self._ats.https_port
        invalid = self._ats.run_shell(f"timeout 5 openssl s_client -alpn banana -connect 127.0.0.1:{port} </dev/null")
        assert invalid.returncode in (0, 1, 124), invalid.output
        self.require_output(invalid, "No ALPN negotiated")

        http1 = self._ats.run_shell(
            f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client -ign_eof -alpn http/1.1 -connect 127.0.0.1:{port}")
        assert http1.returncode == 0, http1.output
        self.require_output(http1, "ALPN protocol: http/1.1", "HTTP/1.1 400 Host Header Required")

        absent = self._ats.run_shell(f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client -ign_eof -connect 127.0.0.1:{port}")
        assert absent.returncode == 0, absent.output
        self.require_output(absent, "No ALPN negotiated", "HTTP/1.1 400 Host Header Required")

    def run_http2_case(self) -> None:
        """Verify curl negotiates h2 and receives an ordinary response."""

        if not self._curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        result = self._curl.run(
            "--insecure",
            "--http2",
            "--verbose",
            "--output",
            "/dev/null",
            f"https://127.0.0.1:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        assert "ALPN: server accepted h2" in result.output
        assert "HTTP/2 404" in result.output

    def run(self) -> None:
        """Start ATS and run every ALPN case sequentially."""

        self._ats.start()
        self.run_openssl_cases()
        self.run_http2_case()


def test_tls_bad_alpn(ats_factory: ATSFactory, curl: Curl) -> None:
    """Unsupported ALPN is declined while supported protocols still work."""

    if shutil.which("openssl") is None:
        pytest.skip("OpenSSL is required")
    AlpnScenario(ats_factory, curl).run()
