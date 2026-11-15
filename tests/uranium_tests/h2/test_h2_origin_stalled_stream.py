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
"""How ATS finishes HTTP/2 origin streams that stall, or that end with an empty DATA frame.

A stalled stream must be reset so the origin can free it. A stream that ends with an empty
END_STREAM DATA frame must deliver its whole body, and must be freed without disturbing the
pooled origin connection, whether or not its transaction is still running.
"""

import http.client
import re
import socket
import sys
import time
from pathlib import Path
from tools.uranium.services import ATS, ATSFactory, ServiceFactory, ProcessService
from .origin_test_helpers import configure_h2_origin, finish_origin

CANCEL = 8
# A body far larger than the client receive buffer and the ATS send buffer that the slow-client test sets. Three
# of them, 60000 bytes, also fit in the origin connection's initial 65535-byte window.
LARGE_SPLIT_SIZE = 20000


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


def get_slowly(ats: ATS, path: str) -> tuple[int, int]:
    """Send one HTTP/1.1 request through ATS from a client that reads late, and return the status and body length.

    With a small receive buffer and a late read, ATS cannot finish writing a large body to the client before the
    origin ends the stream.
    """
    connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
        connection.settimeout(30)
        connection.connect(("127.0.0.1", ats.http_port))
        connection.sendall(f"GET {path} HTTP/1.1\r\nHost: origin.example.com\r\n\r\n".encode())
        time.sleep(1)
        response = http.client.HTTPResponse(connection)
        response.begin()
        return response.status, len(response.read())
    finally:
        connection.close()


def metric(ats: ATS, name: str) -> int:
    """Return the current value of one ATS metric."""
    result = ats.traffic_ctl("metric", "get", name)
    assert result.returncode == 0, result.output
    reported, value = result.stdout.split()
    assert reported == name, result.output
    return int(value)


def wait_for_metric(ats: ATS, name: str, expected: int, timeout: float = 1.5) -> None:
    """Wait for a metric to reach a value, and fail with its last value if it does not."""
    deadline = time.monotonic() + timeout
    while (value := metric(ats, name)) != expected and time.monotonic() < deadline:
        time.sleep(0.1)
    assert value == expected, f"{name} is {value}, expected {expected}"


def start_origin(ats_factory: ATSFactory, services: ServiceFactory, **records: object) -> tuple[ATS, ProcessService, Path]:
    """Start ATS in front of stalled_stream_origin.py, whose paths stall, trickle or end streams in odd ways.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    :param records: Additional record values for ATS.
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
            **records,
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
            "--large-split-size",
            str(LARGE_SPLIT_SIZE),
        ],
        ready_port=port,
    )
    origin.start()
    ats.start()
    return ats, origin, stop


def assert_clean_origin_session(ats: ATS, origin: ProcessService, paths: list[str]) -> None:
    """Check that every request went over the first origin connection and that no stream was reset or failed.

    :param ats: The proxy, still running.
    :param origin: The origin, still running.
    :param paths: Every path the test requested.
    """
    for path in paths:
        origin.wait_for_output(rf"^conn \d+ request stream=\d+ path={re.escape(path)}$")
    requests = re.findall(r"^conn (\d+) request stream=\d+ path=(\S+)$", origin.output, re.MULTILINE)
    assert requests == [("1", path) for path in paths], requests
    assert "reset stream=" not in origin.output, origin.output
    assert "goaway" not in origin.output, origin.output
    wait_for_metric(ats, "proxy.process.http2.current_server_streams", 0)
    # The client side is HTTP/1.1, so every stream error here would be on the origin session.
    assert metric(ats, "proxy.process.http2.stream_errors") == 0


def assert_clean_diags(ats: ATS) -> None:
    """Check that ATS logged no error."""
    diags = ats.diags_log.read_text(errors="replace")
    assert not re.search("ERROR:|FATAL:|Unrecognized configuration value", diags), diags


def test_h2_origin_stalled_stream_is_reset(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Reset a stalled origin stream with CANCEL when its transaction times out, and only that stream.

    The reset is not counted as a stream error. Streams the origin finished, including one whose
    END_STREAM came in its own empty DATA frame, are not reset.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    ats, origin, stop = start_origin(ats_factory, services)

    assert get(ats, "/ok-before") == 200
    assert get(ats, "/split") == 200
    assert get(ats, "/stall") == 504
    output = origin.wait_for_output(r"^conn \d+ request stream=\d+ path=/stall$")
    stalled = re.search(r"^conn (\d+) request stream=(\d+) path=/stall$", output, re.MULTILINE)
    assert stalled is not None
    output = origin.wait_for_output(rf"^conn {stalled[1]} reset stream={stalled[2]} error={CANCEL}$")
    assert get(ats, "/ok-after") == 200

    # The client side is HTTP/1.1, so every stream error here would be on the origin session.
    assert metric(ats, "proxy.process.http2.stream_errors") == 0

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    # Only the stalled stream was cancelled.
    traffic = ats.traffic_out.read_text(errors="replace")
    cancels = re.findall(rf"Send RST_STREAM frame: Error Code: {CANCEL}$", traffic, re.MULTILINE)
    assert len(cancels) == 1, "\n".join(line for line in traffic.splitlines() if "RST_STREAM" in line)
    diags = ats.diags_log.read_text(errors="replace")
    assert not re.search("FATAL:|Unrecognized configuration value", diags), diags


def test_h2_origin_empty_end_stream_reaches_slow_client(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Deliver a whole body that ends with an empty DATA frame to a client that has not read it yet.

    ATS is still writing the body to the client when the origin's END_STREAM arrives, so the end of
    the origin stream must not be taken for the origin giving up.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    ats, origin, stop = start_origin(ats_factory, services, **{"proxy.config.net.sock_send_buffer_size_in": 2048})

    # Three bodies fill most of the origin connection's 65535-byte window, so do not add a fourth.
    paths = [f"/large-split-{attempt}" for attempt in range(3)]
    for path in paths:
        assert get_slowly(ats, path) == (200, LARGE_SPLIT_SIZE)
    assert_clean_origin_session(ats, origin, paths)

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    assert_clean_diags(ats)


def test_h2_origin_late_empty_end_stream_frees_stream(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Free an origin stream whose empty END_STREAM arrives after its transaction has released it.

    The origin sends the whole response at once but ends each stream half a second later. ATS has
    read the whole body and released the stream by then. For the first two requests the
    transaction is also finished, so nothing is left to free the stream. A stream left behind
    would time out at the keep-alive timeout as an HTTP/2 stream error and take the pooled origin
    connection down with it. For the last request the client reads late, so the transaction is
    still writing to it when the END_STREAM arrives, and must not hear about a stream it has let go.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    keep_alive_timeout = 2
    ats, origin, stop = start_origin(
        ats_factory,
        services,
        **{
            "proxy.config.http.keep_alive_no_activity_timeout_out": keep_alive_timeout,
            "proxy.config.net.sock_send_buffer_size_in": 2048,
        },
    )

    paths = ["/late-end-empty", "/late-end-body"]
    for path in paths:
        assert get(ats, path) == 200
        origin.wait_for_output(rf"^conn \d+ ended stream=\d+ path={re.escape(path)}$")
        wait_for_metric(ats, "proxy.process.http2.current_server_streams", 0)
    paths.append("/late-end-large")
    assert get_slowly(ats, "/late-end-large") == (200, LARGE_SPLIT_SIZE)
    wait_for_metric(ats, "proxy.process.http2.current_server_streams", 0)

    # A leaked stream would have timed out by now.
    time.sleep(keep_alive_timeout + 1)
    paths.append("/ok-after")
    assert get(ats, "/ok-after") == 200
    assert_clean_origin_session(ats, origin, paths)

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    assert_clean_diags(ats)
