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
"""HTTP/2 origin streams that ATS gives up on must be reset, so the origin can free them."""

import http.client
import re
import sys
from pathlib import Path
from tools.uranium.services import ATS, ATSFactory, ServiceFactory, ProcessService
from .origin_test_helpers import configure_h2_origin, finish_origin

CANCEL = 8


def get(ats: ATS, path: str) -> int:
    """Send one HTTP/1.1 request through ATS and return the response status."""
    connection = http.client.HTTPConnection("127.0.0.1", ats.http_port, timeout=30)
    try:
        connection.request("GET", path, headers={"Host": "origin.example.com"})
        response = connection.getresponse()
        response.read()
        return response.status
    finally:
        connection.close()


def start_stalling_origin(ats_factory: ATSFactory, services: ServiceFactory) -> tuple[ATS, ProcessService, Path]:
    """Start ATS in front of an origin that never answers /stall requests."""
    port = services.allocate_port()
    ats = configure_h2_origin(
        ats_factory,
        port,
        disable_log_checks=True,
        **{
            "proxy.config.http.transaction_no_activity_timeout_out": 2,
            # A retry would put the stalled request on the origin a second time.
            "proxy.config.http.connect_attempts_max_retries": 0,
        },
    )
    stop = ats.run_directory / "stop-origin"
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin",
        [
            sys.executable,
            Path(__file__).with_name("stalled_stream_origin.py"),
            str(port),
            ssl / "server.pem",
            ssl / "server.key",
            stop,
        ],
        ready_port=port,
    )
    origin.start()
    ats.start()
    return ats, origin, stop


def test_h2_origin_stalled_stream_is_reset(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Reset a stalled origin stream with CANCEL when its transaction times out, and only that stream.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    ats, origin, stop = start_stalling_origin(ats_factory, services)

    assert get(ats, "/ok-before") == 200
    assert get(ats, "/stall") == 504
    output = origin.wait_for_output(r"^conn \d+ request stream=\d+ path=/stall$")
    stalled = re.search(r"^conn (\d+) request stream=(\d+) path=/stall$", output, re.MULTILINE)
    assert stalled is not None
    output = origin.wait_for_output(rf"^conn {stalled[1]} reset stream={stalled[2]} error={CANCEL}$")
    assert get(ats, "/ok-after") == 200

    output = origin.wait_for_output(r"^conn \d+ request stream=\d+ path=/ok-after$")
    # Streams that finished normally are not reset.
    assert len(re.findall(r"^conn \d+ reset ", output, re.MULTILINE)) == 1, output

    finish_origin(services, origin, stop)
    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    assert not re.search("FATAL:|Unrecognized configuration value", diags), diags
