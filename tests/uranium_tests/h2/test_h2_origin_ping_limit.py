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
"""Limit PING frames from origins independently of PING frames from clients."""

import re
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_metric
from .origin_test_helpers import configure_h2_origin, finish_origin

ENHANCE_YOUR_CALM = 11

CASES = [
    pytest.param(
        {"proxy.config.http2.max_ping_frames_per_minute": 5, "proxy.config.http2.max_ping_frames_per_minute_out": 1000},
        # Above the 300 default too, so the case fails if ATS ignores the _out record.
        400,
        True,
        False,
        id="origin-limit-above-client-limit",
    ),
    pytest.param(
        {"proxy.config.http2.max_ping_frames_per_minute": 1000, "proxy.config.http2.max_ping_frames_per_minute_out": 5},
        10,
        False,
        True,
        id="origin-limit-below-client-limit",
    ),
    pytest.param({}, 60, False, False, id="client-default-at-limit"),
    pytest.param({}, 61, True, False, id="client-default-over-limit"),
    pytest.param({}, 300, True, False, id="origin-default-at-limit"),
    pytest.param({}, 301, True, True, id="origin-default-over-limit"),
]


@pytest.mark.parametrize("records,ping_count,client_closed,origin_closed", CASES)
def test_h2_origin_ping_limit(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    records: dict[str, int],
    ping_count: int,
    client_closed: bool,
    origin_closed: bool,
) -> None:
    """Send the same PING burst from a client and from an origin and check which side ATS closes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the PING origin and both client connections.
    :param records: PING limits under test.
    :param ping_count: PING frames each peer sends in one burst.
    :param client_closed: Whether ATS should close the client connection with ENHANCE_YOUR_CALM.
    :param origin_closed: Whether ATS should close the origin connection with ENHANCE_YOUR_CALM.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    ats = configure_h2_origin(
        ats_factory,
        port,
        disable_log_checks=True,
        **{
            # A retry would open a second origin session and send a second burst.
            "proxy.config.http.connect_attempts_max_retries": 0,
            **records,
        },
    )
    stop = ats.run_directory / "stop-origin"
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin",
        [sys.executable, directory / "ping_origin.py", str(port), ssl / "server.pem", ssl / "server.key", str(ping_count), stop],
        ready_port=port,
    )
    origin.start()
    ats.start()

    result = services.process(
        "client-pings", [sys.executable, directory / "ping_client.py", str(ats.https_port), str(ping_count)]
    ).run(timeout=30)
    assert result.returncode == 0, result.output
    if client_closed:
        assert f"client_goaway error={ENHANCE_YOUR_CALM}" in result.stdout, result.output
    else:
        assert f"client_ping_acks={ping_count}" in result.stdout, result.output

    result = services.process("client-get", [sys.executable, directory / "ping_client.py", str(ats.https_port), "0"]).run(
        timeout=30
    )
    assert result.returncode == 0, result.output
    origin_output = origin.wait_for_output(r"^(peer_goaway|peer_eof|ping_acks=)", timeout=15)
    assert origin_output.count("request_received") == 1, origin_output
    if origin_closed:
        assert f"peer_goaway error={ENHANCE_YOUR_CALM}" in origin_output, origin_output
        assert "client_status=502" in result.stdout, result.output
    else:
        assert re.search(f"^ping_acks={ping_count}$", origin_output, re.MULTILINE), origin_output
        assert "client_status=200" in result.stdout, result.output

    wait_for_metric(ats, "proxy.process.http2.max_ping_frames_per_minute_exceeded", int(client_closed) + int(origin_closed))
    assert ats.is_running
    finish_origin(services, origin, stop)
    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    # Each closed connection logs its ENHANCE_YOUR_CALM error; any other ERROR fails the test.
    assert not re.search("ERROR:(?! HTTP/2 connection error code=0x0b.*recv ping too frequent PING frame)", diags), diags
    assert not re.search("FATAL:|Unrecognized configuration value", diags), diags
