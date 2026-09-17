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
"""Verify background_fetch Content-Length and wildcard exclusion rules."""

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


class BackgroundFetchRuleScenario:
    """Exercise background-fetch rule matching for range requests."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Configure the origin, ATS, and transport-aware client.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param services: Factory for the microserver origin.
        :param curl: Transport-aware curl command runner.
        """

        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create range responses and the full responses fetched in the background.

        :param services: Factory for the microserver origin.
        """

        origin = services.origin("origin", lookup_key="{PATH}{%Range}")
        for path, host in (
            ("allowed", "allowed.example"),
            ("small", "small.example"),
            ("wildcard", "wildcard.example"),
            ("above-threshold", "above-threshold.example"),
        ):
            origin.add_response(
                {
                    "headers": f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nAccept: */*\r\nRange: bytes=0-4\r\n\r\n",
                    "body": "",
                },
                {
                    "headers":
                        (
                            "HTTP/1.1 206 Partial Content\r\nConnection: close\r\nCache-Control: max-age=600\r\n"
                            "Content-Range: bytes 0-4/10\r\nContent-Length: 5\r\n\r\n"),
                    "body": "hello",
                },
            )
        for path, host in (("allowed", "allowed.example"), ("above-threshold", "above-threshold.example")):
            origin.add_response(
                {
                    "headers": f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nAccept: */*\r\n\r\n",
                    "body": ""
                },
                {
                    "headers":
                        ("HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=600\r\n"
                         "Content-Length: 10\r\n\r\n"),
                    "body": "hellohello",
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure one background_fetch remap for each rule case.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("background_fetch.so"):
            pytest.skip("background_fetch.so is required")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "background_fetch",
        })
        ats.write_config_file("background_fetch_small.config", "exclude Content-Length <1000\n")
        ats.write_config_file("background_fetch_above_threshold.config", "exclude Content-Length >1000\n")
        ats.write_config_file("background_fetch_wildcard.config", "exclude X-Skip-Bg *\n")
        mappings = (
            ("allowed.example", ""),
            ("small.example", " @pparam=background_fetch_small.config"),
            ("wildcard.example", " @pparam=background_fetch_wildcard.config"),
            ("above-threshold.example", " @pparam=background_fetch_above_threshold.config"),
        )
        for hostname, parameter in mappings:
            ats.remap_config.add_line(
                f"map http://{hostname}/ http://127.0.0.1:{self._origin.port}/ "
                f"@plugin=background_fetch.so{parameter}")
        return ats

    def request_range(self, hostname: str, path: str, marker: str, *, skip: bool = False) -> None:
        """Send one partial request and require the 206 response.

        :param hostname: Host header selecting the remap rule.
        :param path: Request path identifying the origin response.
        :param marker: Value logged when a background request starts.
        :param skip: Whether to send the wildcard exclusion header.
        """

        headers = {
            "Host": hostname,
            "Range": "bytes=0-4",
            "X-Background-Fetch-Test": marker,
        }
        if skip:
            headers["X-Skip-Bg"] = "yes"
        result = self._curl.get(self._ats, path, headers=headers, options="--silent --dump-header -")
        assert result.returncode == 0, result.output
        assert "206 Partial Content" in result.stdout, result.output

    def run(self) -> None:
        """Run permitted and excluded background-fetch cases and inspect diagnostics."""

        self._origin.start()
        self._ats.start()
        self.request_range("allowed.example", "/allowed", "allowed")
        self.request_range("small.example", "/small", "small")
        self.request_range("wildcard.example", "/wildcard", "wildcard", skip=True)
        self.request_range("above-threshold.example", "/above-threshold", "above-threshold")

        wait_for_file_lines(
            self._ats.traffic_out,
            r"X-Background-Fetch-Test: above-threshold",
            1,
            timeout=30,
        )
        self._ats.stop()
        output = self._ats.traffic_out.read_text(errors="replace")
        assert re.search(r"adding background_fetch content length rule .* for Content-Length: 1000", output)
        assert "found exclude rule match" in output
        assert "Found X-Skip-Bg wild card" in output
        assert "Starting background fetch, replaying:" in output
        for marker in ("allowed", "above-threshold"):
            assert f"X-Background-Fetch-Test: {marker}" in output
        for marker in ("small", "wildcard"):
            assert f"X-Background-Fetch-Test: {marker}" not in output


def test_background_fetch(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Background fetch applies Content-Length and wildcard exclusions.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the microserver origin.
    :param curl: Transport-aware curl command runner.
    """

    BackgroundFetchRuleScenario(ats_factory, services, curl).run()
