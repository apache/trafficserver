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


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the HTTP/1.1 origin response used by every request.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /get HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure matching ASCII, v2 binary, and v3 binary log objects.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({
        "proxy.config.log.max_secs_per_buffer": 1,
        "proxy.config.log.periodic_tasks_interval": 1,
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}/")
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


def generate_traffic(*, _ats: ATS, _curl: Curl, _origin: OriginServer) -> None:
    """Generate three origin-backed log entries and await their flush.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    _origin.start()
    _ats.start()
    for _ in range(3):
        result = _curl.get(_ats, "/get", options=f"--http1.1")
        assert result.returncode == 0, result.output
    wait_for_file_lines(_ats.log_directory / "ascii.log", r"/get", 3)


def decode(*arguments: str, _ats: ATS) -> str:
    """Decode one log with traffic_logcat.

    :param _ats: Test-local ats configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _ats.run("traffic_logcat", *arguments)
    assert result.returncode == 0, result.output
    return result.stdout


def test_binary_log_v3(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Traffic_logcat reads v2 and self-describing v3 binary logs.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("The client-address log field requires a TCP curl connection")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    generate_traffic(_ats=_ats, _curl=curl, _origin=_origin)
    v2_blog = _ats.log_directory / "v2.blog"
    v3_blog = _ats.log_directory / "v3.blog"
    gold = TEST_DIRECTORY / "gold"
    assert_matches_gold(decode(str(v2_blog), _ats=_ats), gold / "binary_log_v3_ascii.gold")
    assert_matches_gold(decode(str(v3_blog), _ats=_ats), gold / "binary_log_v3_ascii.gold")
    assert_matches_gold(decode("-j", str(v3_blog), _ats=_ats), gold / "binary_log_v3_json.gold")

    v3_header = decode("-H", str(v3_blog), _ats=_ats)
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

    v2_header = decode("-H", str(v2_blog), _ats=_ats)
    assert re.search(r"version:\s+2", v2_header), v2_header
    assert re.search(r"fieldlist:\s+chi,cqu,pssc,sshv", v2_header), v2_header
    assert "field_type_schema" not in v2_header
