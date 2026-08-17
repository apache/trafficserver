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

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class BinaryLogV3Scenario:
    """Write v2 and v3 binary logs and decode both formats."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("The client-address log field requires a TCP curl connection")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the HTTP/1.1 origin response used by every request."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET /get HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure matching ASCII, v2 binary, and v3 binary log objects."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update({
            "proxy.config.log.max_secs_per_buffer": 1,
            "proxy.config.log.periodic_tasks_interval": 1,
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}/")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "custom_fmt",
                            "format": "%<chi> %<cqu> %<pssc> %<sshv>"
                        }],
                        "logs":
                            [
                                {
                                    "filename": "v2",
                                    "format": "custom_fmt",
                                    "mode": "binary",
                                    "binary_log_version": 2
                                },
                                {
                                    "filename": "v3",
                                    "format": "custom_fmt",
                                    "mode": "binary",
                                    "binary_log_version": 3
                                },
                                {
                                    "filename": "ascii",
                                    "format": "custom_fmt",
                                    "mode": "ascii"
                                },
                            ],
                    }
            })
        return ats

    def generate_traffic(self) -> None:
        """Generate three origin-backed log entries and await their flush."""

        self._origin.start()
        self._ats.start()
        for _ in range(3):
            result = self._curl.get(self._ats, "/get", options=f"--http1.1")
            assert result.returncode == 0, result.output
        wait_for_file_lines(self._ats.log_directory / "ascii.log", r"/get", 3)

    def decode(self, *arguments: str) -> str:
        """Decode one log with traffic_logcat."""

        result = self._ats.run("traffic_logcat", *arguments)
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Verify data decoding and the version-specific segment schemas."""

        self.generate_traffic()
        v2_blog = self._ats.log_directory / "v2.blog"
        v3_blog = self._ats.log_directory / "v3.blog"
        gold = TEST_DIRECTORY / "gold"
        assert_matches_gold(self.decode(str(v2_blog)), gold / "binary_log_v3_ascii.gold")
        assert_matches_gold(self.decode(str(v3_blog)), gold / "binary_log_v3_ascii.gold")
        assert_matches_gold(self.decode("-j", str(v3_blog)), gold / "binary_log_v3_json.gold")

        v3_header = self.decode("-H", str(v3_blog))
        for expression in (
                r"version:\s+3",
                r"format_type:\s+4 \(CUSTOM\)",
                r"fieldlist:\s+chi,cqu,pssc,sshv",
                r"field_type_schema:\s+field_count=4",
                r"chi\s+IP",
                r"cqu\s+STRING",
                r"pssc\s+sINT",
                r"sshv\s+STRING",
        ):
            assert re.search(expression, v3_header), v3_header

        v2_header = self.decode("-H", str(v2_blog))
        assert re.search(r"version:\s+2", v2_header), v2_header
        assert re.search(r"fieldlist:\s+chi,cqu,pssc,sshv", v2_header), v2_header
        assert "field_type_schema" not in v2_header


def test_binary_log_v3(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Traffic_logcat reads v2 and self-describing v3 binary logs."""

    BinaryLogV3Scenario(ats_factory, services, curl).run()
