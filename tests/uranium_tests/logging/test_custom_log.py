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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, assert_matches_gold, wait_for_file_lines


class CustomLogAddressScenario:
    """Log the dotted and hexadecimal form of each inbound destination IP."""

    ADDRESSES = (
        "127.0.0.1",
        "127.1.1.1",
        "127.2.2.2",
        "127.3.3.3",
        "127.3.0.1",
        "127.43.2.1",
        "127.213.213.132",
        "127.123.32.243",
    )

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self._curl = curl
        self._gold = Path(__file__).parent / "gold" / "custom.gold"
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a denied remap and the destination-address log fields."""

        ats = ats_factory.create("ts")
        ats.records.update({"proxy.config.log.max_secs_per_buffer": 1})
        ats.remap_config.add_line("map / http://www.linkedin.com/ @action=deny")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "custom",
                            "format": "%<hii> %<hiih>"
                        }],
                        "logs": [{
                            "filename": "test_log_field",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def send_requests(self) -> None:
        """Address the same listener through distinct Linux loopback IPs."""

        for address in self.ADDRESSES:
            result = self._curl.run(f"http://{address}:{self._ats.http_port}", "--verbose")
            assert result.returncode == 0, result.output

    def run(self) -> None:
        """Generate and verify all custom log lines."""

        if sys.platform != "linux":
            pytest.skip("This test depends on Linux loopback addressing")
        if self._curl.uses_uds:
            pytest.skip("Destination IP fields require TCP curl connections")
        self._ats.start()
        self.send_requests()
        path = self._ats.log_directory / "test_log_field.log"
        content = wait_for_file_lines(path, r"^127\.", len(self.ADDRESSES))
        assert_matches_gold(content, self._gold)


def test_custom_log(ats_factory: ATSFactory, curl: Curl) -> None:
    """Custom logs preserve all IPv4 destination address bits."""

    CustomLogAddressScenario(ats_factory, curl).run()
