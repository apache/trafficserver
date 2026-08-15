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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class S3UrlEncodingScenario:
    """Exercise S3 signing for encoded and mixed-encoded request paths."""

    _cases = (
        ("/bucket/app/(channel)/test.js", "test1ok"),
        ("/bucket/app/%28channel%29/test.js", "test2ok"),
        ("/bucket/app/(channel)/%5B%5Bparts%5D%5D/page.js", "test3ok"),
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create one expected origin transaction for each path representation."""

        origin = services.origin("origin")
        for path, body in self._cases:
            origin.add_response(
                {
                    "headers": f"GET {path} HTTP/1.1\r\nHost: s3.amazonaws.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: {len(body)}\r\n\r\n",
                    "body": body,
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure origin_server_auth with fixed S3 test credentials."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("origin_server_auth.so"):
            pytest.skip("origin_server_auth.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "origin_server_auth",
                "proxy.config.url_remap.pristine_host_hdr": 1,
            })
        config = TEST_DIRECTORY / "rules" / "s3_url_encoding.test_input"
        ats.copy_to_config(config)
        ats.remap_config.add_line(
            f"map http://s3.amazonaws.com/ http://127.0.0.1:{self._origin.port}/ "
            f"@plugin=origin_server_auth.so @pparam=--config @pparam={ats.config_directory / config.name}")
        return ats

    def run(self) -> None:
        """Require every path form to be signed and forwarded successfully."""

        self._origin.start()
        self._ats.start()
        for path, body in self._cases:
            result = self._curl.run_for(
                self._ats,
                "--silent",
                "--verbose",
                "--path-as-is",
                "--header",
                "Host: s3.amazonaws.com",
                f"http://127.0.0.1:{self._ats.http_port}{path}",
            )
            assert result.returncode == 0, result.output
            assert "200 OK" in result.stderr
            assert result.stdout == body


def test_s3_url_encoding(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Mixed URL escaping does not prevent S3 request signing."""

    S3UrlEncodingScenario(ats_factory, services, curl).run()
