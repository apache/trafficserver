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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class OriginServerAuthScenario:
    """Verify origin_server_auth file parsing and GCP token configuration."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._rules = TEST_DIRECTORY / "rules" / "v4-parse-test.test_input"
        self._token = next(
            line.removeprefix("session_token=").strip()
            for line in self._rules.read_text().splitlines()
            if line.startswith("session_token="))
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("origin_server_auth.so"):
            pytest.skip("origin_server_auth.so is required")

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create successful responses for the S3 and GCP paths."""

        origin = services.origin("server")
        for path in ("s3-bucket", "gcp"):
            origin.add_response(
                {"headers": f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                    "body": "success!"
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure file-based AWS v4 and inline GCP authentication."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.show_location": 0,
                "proxy.config.diags.debug.tags": "origin_server_auth",
            })
        ats.copy_to_config(self._rules)
        rules_path = ats.config_directory / self._rules.name
        ats.remap_config.add_lines(
            (
                f"map http://www.example.com/s3-bucket http://127.0.0.1:{self._origin.port}/s3-bucket "
                f"@plugin=origin_server_auth.so @pparam=--config @pparam={rules_path}",
                f"map http://www.example.com/gcp http://127.0.0.1:{self._origin.port}/gcp "
                f"@plugin=origin_server_auth.so @pparam=--access_key @pparam=1234567 "
                f"@pparam=--session_token @pparam={self._token} @pparam=--version @pparam=gcpv1",
            ))
        return ats

    def request(self, path: str) -> None:
        """Request one authenticated origin path."""

        result = self._curl.get(
            self._ats,
            f"/{path}",
            headers={"Host": "www.example.com"},
            options=("--silent", "--verbose"),
        )
        assert result.returncode == 0, result.output
        assert "200 OK" in result.stderr
        assert "Content-Length: 8" in result.stderr

    def run(self) -> None:
        """Exercise both configurations and compare the parsing diagnostics."""

        self._origin.start()
        self._ats.start()
        self.request("s3-bucket")
        self.request("gcp")
        gold = "origin_server_auth_parsing_ts_uds.gold" if self._curl.uses_uds else "origin_server_auth_parsing_ts.gold"
        assert_matches_gold(self._ats.traffic_out.read_text(errors="replace"), TEST_DIRECTORY / "gold" / gold)


def test_origin_server_auth(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """origin_server_auth parses long file values and inline GCP configuration."""

    OriginServerAuthScenario(ats_factory, services, curl).run()
