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
"""Verify compliant pacing, periodic gauges, and state clearing."""

from pathlib import Path
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the cacheable two-byte origin response.

    :param services: Factory for the microserver origin.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\nCache-Control: max-age=300\r\n\r\n",
            "body": "OK",
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure combined and paced rules.

    :param ats_factory: Factory for isolated Traffic Server instances.

    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create("ts", enable_cache=True)
    if not ats.plugin_exists("abuse_shield.so"):
        pytest.skip("abuse_shield.so is required")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.write_config_file(
        "pace.yaml",
        "global: {ip_tracking: {slots: 10}, log_file: abuse_shield}\n"
        "rules:\n"
        "  - {name: combined, filter: {max_req_rate: 1, max_conn_rate: 5}, action: [block, close]}\n"
        "  - {name: paced, filter: {max_req_rate: 30}, action: [log, block, close]}\n",
    )
    ats.plugin_config.add_line("abuse_shield.so pace.yaml")
    return ats


def check_metrics(events: int, slots: int, *, _ats: ATS) -> None:
    """Verify event counters and current slot usage.

    :param events: Expected cumulative transaction events.
    :param slots: Expected current tracking slots.

    :param _ats: Test-local ats configured by the test.
    """

    expected = {
        "txn.events": events,
        "txn.slots_used": slots,
        "rules.matched": 0,
        "actions.blocked": 0,
        "actions.closed": 0,
        "connections.rejected": 0,
    }
    result = _ats.traffic_ctl("metric", "get", *(f"abuse_shield.{name}" for name in expected))
    assert result.returncode == 0, result.output
    for name, value in expected.items():
        assert re.search(rf"abuse_shield\.{re.escape(name)}\s+{value}\b", result.stdout), result.output


def test_abuse_shield_pacing(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Compliant traffic does not falsely match either rule.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the origin and paced client.
    """

    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = services.process(
        "paced-client",
        (sys.executable, TEST_DIRECTORY / "paced_requests.py", "--port", str(_ats.http_port)),
    )

    _origin.start()
    _ats.start()
    result = _client.run(timeout=30)
    assert result.returncode == 0, result.output
    check_metrics(300, 1, _ats=_ats)
    clear = _ats.traffic_ctl("plugin", "msg", "abuse_shield.clear")
    assert clear.returncode == 0, clear.output
    wait_for_file_lines(_ats.diags_log, "Tracking and block state cleared", 1)
    check_metrics(300, 0, _ats=_ats)
