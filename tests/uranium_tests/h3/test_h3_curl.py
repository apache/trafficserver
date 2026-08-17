#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class H3CurlScenario:
    """Verify HTTP/3 interoperability with curl."""

    _response_body = "0123456789" * 30000

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        curl_help = subprocess.check_output(("curl", "--help", "all"), text=True)
        if not ats_factory.has_feature("TS_USE_QUIC") or not Curl.supports("http3") or "--http3-only" not in curl_help:
            pytest.skip("ATS QUIC and curl HTTP/3 support are required")
        if curl.uses_uds:
            pytest.skip("HTTP/3 requires a UDP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the large HTTP/3 response body."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET /h3-curl HTTP/1.1\r\nHost: localhost\r\n\r\n"},
            {
                "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: {len(self._response_body)}\r\n\r\n",
                "body": self._response_body,
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure QUIC, the foo.com certificate, and access logging."""

        ats = ats_factory.create("ts", enable_tls=True, enable_quic=True, enable_cache=False)
        ats.set_startup_timeout(60)
        ats.add_default_ssl_files()
        ats.copy_to_ssl(
            TEST_DIRECTORY.parent / "tls" / "ssl" / "signed-foo.pem",
            TEST_DIRECTORY.parent / "tls" / "ssl" / "signed-foo.key",
        )
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                "  - ssl_cert_name: signed-foo.pem",
                "    ssl_key_name: signed-foo.key",
                "  - ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
                '    dest_ip: "*"',
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "quic|http3",
                "proxy.config.quic.server.stateless_retry_enabled": 0,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "h3_access",
                                    "format":
                                        (
                                            "c_alpn=%<cqssa> client_version=%<cqpv> c_ssl_version=%<cqssv> "
                                            "c_method=%<cqhm> c_url=%<cquuc>"),
                                }
                            ],
                        "logs": [{
                            "filename": "h3_access",
                            "format": "h3_access"
                        }],
                    }
            })
        return ats

    def run(self) -> None:
        """Fetch the full object over HTTP/3 and inspect its access log."""

        self._origin.start()
        self._ats.start()
        body = self._ats.run_directory.parent / "h3_curl_body.txt"
        result = self._curl.run_for(
            self._ats,
            (
                f"--silent --show-error --fail --ipv4 --http3-only --insecure --resolve "
                f"'foo.com:{self._ats.https_port}:127.0.0.1' --output '{str(body)}' --write-out "
                f"'\nhttp_version=%{{http_version}}\nsize_download=%{{size_download}}\n' "
                f"'https://foo.com:{self._ats.https_port}/h3-curl'"),
            timeout=30,
        )
        assert result.returncode == 0, result.output
        assert f"size_download={len(self._response_body)}" in result.stdout
        assert "http_version=3" in result.stdout
        assert body.read_text() == self._response_body
        content = wait_for_file_lines(self._ats.log_directory / "h3_access.log", r"c_alpn=h3", 1, timeout=10)
        assert re.search(
            r"c_alpn=h3 client_version=http/3 c_ssl_version=[^ ]+ c_method=GET "
            r"c_url=https://foo\.com:[0-9]+/h3-curl",
            content,
        )


def test_h3_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """curl completes a forced HTTP/3 request through ATS."""

    H3CurlScenario(ats_factory, services, curl).run()
