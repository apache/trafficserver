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
"""An HTTP/2 origin session must stop taking new streams once one of its streams times out."""

import http.client
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin, finish_origin


def get(port: int, path: str) -> int:
    """Send one HTTP/1.1 request through ATS and return the response status."""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
    try:
        connection.request("GET", path, headers={"Host": "origin.example.com"})
        response = connection.getresponse()
        response.read()
        return response.status
    finally:
        connection.close()


def test_h2_origin_session_retired_after_stream_timeout(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Stop giving an origin session new streams once one of its streams times out.

    A stream that is still busy on the session when that happens finishes normally, the next
    request opens a new connection, and the old one closes once it has no streams left.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
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
    # The /slow stream trickles its body for 4 s: it outlives the 2 s timeout of /stall, but it
    # never goes idle long enough to time out itself.
    origin = services.process(
        "origin",
        [
            sys.executable,
            Path(__file__).with_name("stalled_stream_origin.py"),
            str(port),
            ssl / "server.pem",
            ssl / "server.key",
            stop,
            "--slow-delay",
            "4",
        ],
        ready_port=port,
    )
    origin.start()
    ats.start()

    assert get(ats.http_port, "/ok-before") == 200
    origin.wait_for_output(r"^conn 1 request stream=\d+ path=/ok-before$")
    with ThreadPoolExecutor(max_workers=2) as pool:
        stalled = pool.submit(get, ats.http_port, "/stall")
        origin.wait_for_output(r"^conn 1 request stream=\d+ path=/stall$")
        busy = pool.submit(get, ats.http_port, "/slow")
        origin.wait_for_output(r"^conn 1 request stream=\d+ path=/slow$")
        assert stalled.result() == 504
        assert busy.result() == 200

    assert get(ats.http_port, "/ok-after") == 200
    output = origin.wait_for_output(r"^conn \d+ request stream=\d+ path=/ok-after$")
    assert re.search(r"^conn 2 request stream=\d+ path=/ok-after$", output, re.MULTILINE), output
    output = origin.wait_for_output(r"^conn 1 (goaway error=0|eof)")
    assert re.search(r"^conn 1 answered stream=\d+ path=/slow$", output, re.MULTILINE), output

    finish_origin(services, origin, stop)
    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    assert not re.search("FATAL:|Unrecognized configuration value", diags), diags
