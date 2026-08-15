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
import json
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, HttpBinServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class HttpbinH2Scenario:
    """Exercise HTTP/2 requests against a go-httpbin origin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._httpbin = self.configure_httpbin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_httpbin(services: ServiceFactory) -> HttpBinServer:
        """Create the HTTP behavior origin."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Terminate HTTP/2, add Via headers, and configure access logging."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._httpbin.port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.http.insert_request_via_str": 1,
                "proxy.config.http.insert_response_via_str": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
            })
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "access",
                                    "format": "[%<cqtn>] %<cqhm> %<pqu> %<cqpv> %<cqssv> %<cqssc> %<crc> %<pssc> %<pscl>",
                                }
                            ],
                        "logs": [{
                            "filename": "access",
                            "format": "access"
                        }],
                    }
            })
        return ats

    def request(self, path: str, *arguments: str) -> CommandResult:
        """Send one verbose HTTP/2 request through ATS."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--silent",
            "--insecure",
            "--http2",
            *arguments,
            f"https://127.0.0.1:{self._ats.https_port}{path}",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/2 200" in result.stderr
        return result

    def run(self) -> None:
        """Verify JSON, empty, streamed, and 100-Continue responses."""

        if not self._curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        if shutil.which("cksum") is None:
            pytest.skip("cksum is required")
        self._httpbin.start()
        self._ats.start()

        basic = self.request("/get")
        assert json.loads(basic.stdout)["url"].endswith("/get")
        assert "via:" in basic.stderr.lower()

        empty = self.request("/bytes/0")
        assert empty.stdout == ""
        assert "content-length: 0" in empty.stderr.lower()

        stream = self._ats.run_shell(
            f"curl -sk --http2 https://127.0.0.1:{self._ats.https_port}/stream-bytes/102400?seed=0 | cksum")
        assert stream.returncode == 0, stream.output
        assert stream.stdout == "3197674613 102400\n"

        post = self.request(
            "/post",
            "--data",
            "key=value",
            "--header",
            "Expect: 100-continue",
            "--max-time",
            "5",
        )
        assert "HTTP/2 100" in post.stderr
        assert json.loads(post.stdout)["form"] == {"key": ["value"]}

        access = wait_for_file_lines(self._ats.log_directory / "access.log", r"POST .*?/post", 1)
        for fragment in ("GET http://127.0.0.1:", "/bytes/0 http/2", "/stream-bytes/102400?seed=0 http/2"):
            assert fragment in access


def test_httpbin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP/2 correctly proxies common httpbin response behaviors."""

    HttpbinH2Scenario(ats_factory, services, curl).run()
