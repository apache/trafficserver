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

CUSTOM_LOG_ADDRESS_ADDRESSES = (
    "127.0.0.1",
    "127.1.1.1",
    "127.2.2.2",
    "127.3.3.3",
    "127.3.0.1",
    "127.43.2.1",
    "127.213.213.132",
    "127.123.32.243",
)


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure a denied remap and the destination-address log fields.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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


def send_requests(*, _ats: ATS, _curl: Curl) -> None:
    """Address the same listener through distinct Linux loopback IPs.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    for address in CUSTOM_LOG_ADDRESS_ADDRESSES:
        result = _curl.run(f"'http://{address}:{_ats.http_port}' --verbose",)
        assert result.returncode == 0, result.output


def test_custom_log(ats_factory: ATSFactory, curl: Curl) -> None:
    """Custom logs preserve all IPv4 destination address bits.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    _gold = Path(__file__).parent / "gold" / "custom.gold"
    _ats = configure_ats(ats_factory)

    if sys.platform != "linux":
        pytest.skip("This test depends on Linux loopback addressing")
    if curl.uses_uds:
        pytest.skip("Destination IP fields require TCP curl connections")
    _ats.start()
    send_requests(_ats=_ats, _curl=curl)
    path = _ats.log_directory / "test_log_field.log"
    content = wait_for_file_lines(path, r"^127\.", len(CUSTOM_LOG_ADDRESS_ADDRESSES))
    assert_matches_gold(content, _gold)
