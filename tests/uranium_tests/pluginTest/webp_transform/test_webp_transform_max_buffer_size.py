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

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TWO_MIB = 2 * 1024 * 1024


class WebpBufferOverrideScenario:
    """Serve an image larger than an explicit one-MiB transform buffer."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve a two-MiB image body."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /two_mib.jpg HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers":
                    ("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
                     f"Content-Length: {TWO_MIB}\r\nConnection: close\r\n\r\n"),
                "body": "A" * TWO_MIB,
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Set max_buffer_size with the M suffix."""

        ats = ats_factory.create("ts", enable_cache=False)
        if not ats.plugin_exists("webp_transform.so"):
            pytest.skip("webp_transform.so is required")
        ats.plugin_config.add_line("webp_transform.so convert_to_webp max_buffer_size=1M")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "webp_transform",
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    @staticmethod
    def verify(result: CommandResult) -> None:
        """Require a truthful passthrough response."""

        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200" in result.stdout
        assert re.search(r"content-type: image/jpeg", result.stdout, re.IGNORECASE)

    def run(self) -> None:
        """Request the image and verify the one-MiB cap was applied."""

        self._origin.start()
        self._ats.start()
        result = self._curl.get(
            self._ats,
            "/two_mib.jpg",
            headers={"Accept": "image/webp"},
            options=("--silent", "--show-error", "--dump-header", "-", "--output", "/dev/null"),
        )
        self.verify(result)
        wait_for_file_lines(self._ats.traffic_out, "exceeds cap 1048576", 1)


class WebpInvalidBufferSizeScenario:
    """Load malformed size values and retain the safe default cap."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure negative, bad-suffix, and multiplication-overflow values."""

        ats = ats_factory.create("ts", enable_cache=False)
        if not ats.plugin_exists("webp_transform.so"):
            pytest.skip("webp_transform.so is required")
        ats.plugin_config.add_line(
            "webp_transform.so convert_to_webp max_buffer_size=-1 "
            "max_buffer_size=8X max_buffer_size=20000000000G")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "webp_transform",
        })
        return ats

    def run(self) -> None:
        """Start ATS and require every parse failure diagnostic."""

        self._ats.start()
        diags = wait_for_file_lines(self._ats.diags_log, "invalid max_buffer_size=", 3)
        for value in ("-1", "8X", "20000000000G"):
            assert f"invalid max_buffer_size={value}, keeping default 16777216" in diags


def test_webp_transform_max_buffer_size(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An M-suffixed max_buffer_size overrides the default transform cap."""

    WebpBufferOverrideScenario(ats_factory, services, curl).run()


def test_webp_transform_invalid_buffer_sizes(ats_factory: ATSFactory) -> None:
    """Malformed max_buffer_size values cannot disable the safe default."""

    WebpInvalidBufferSizeScenario(ats_factory).run()
