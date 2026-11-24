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

Every client here speaks HTTP/1.1, so every HTTP/2 stream and stream error is on the origin session.
"""

import http.client
import re
import socket
import sys
import time
from collections.abc import Callable
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


def get_slowly(ats: ATS, path: str, wait: Callable[[], object] = lambda: time.sleep(1)) -> tuple[int, int]:
    """Send one HTTP/1.1 request through ATS from a client that reads late, and return the status and body length.

    With a small receive buffer and a late read, ATS cannot finish writing a large body to the client before the
    origin ends the stream.

    :param wait: Called after the request is sent; the client reads nothing until it returns.
    """
    connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
        connection.settimeout(30)
        connection.connect(("127.0.0.1", ats.http_port))
        connection.sendall(f"GET {path} HTTP/1.1\r\nHost: origin.example.com\r\n\r\n".encode())
        wait()
        response = http.client.HTTPResponse(connection)
        response.begin()
        return response.status, len(response.read())
    finally:
        connection.close()


def get_timed(ats: ATS, path: str) -> tuple[int, int, float]:
    """Send one HTTP/1.1 request through ATS and return the status, the body length and the seconds it took.

    A body that ATS cuts off counts the bytes that arrived before the cut.
    """
    start = time.monotonic()
    connection = http.client.HTTPConnection("127.0.0.1", ats.http_port, timeout=30)
    try:
        connection.request("GET", path, headers={"Host": "origin.example.com"})
        response = connection.getresponse()
        try:
            length = len(response.read())
        except http.client.IncompleteRead as error:
            length = len(error.partial)
        return response.status, length, time.monotonic() - start
    finally:
        connection.close()


def metric(ats: ATS, name: str) -> int:
    """Return the current value of one ATS metric."""
    result = ats.traffic_ctl("metric", "get", name)
    assert result.returncode == 0, result.output
    reported, value = result.stdout.split()
    assert reported == name, result.output
    return int(value)


def wait_for_value(read: Callable[[], int], expected: int, what: str, timeout: float = 1.5) -> None:
    """Wait for read() to return a value, and fail with its last value if it does not."""
    deadline = time.monotonic() + timeout
    while (value := read()) != expected and time.monotonic() < deadline:
        time.sleep(0.1)
    assert value == expected, f"{what} is {value}, expected {expected}"


def wait_for_metric(ats: ATS, name: str, expected: int, timeout: float = 1.5) -> None:
    """Wait for a metric to reach a value, and fail with its last value if it does not."""
    wait_for_value(lambda: metric(ats, name), expected, name, timeout)


def start_origin(
    ats_factory: ATSFactory, services: ServiceFactory, plugin: str | None = None, **records: object
) -> tuple[ATS, ProcessService, Path]:
    """Start ATS in front of stalled_stream_origin.py, whose paths stall, trickle or end streams in odd ways.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    :param plugin: A plugin.config line for a test plugin to load, if any.
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
    if plugin is not None:
        ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{plugin.split()[0]}")
        ats.plugin_config.add_line(plugin)
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
    """Check that every request went over the first origin connection, that every stream was freed, and that no
    stream was reset or failed.

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
    # current_server_streams drops when the transaction lets go of the stream, not when the stream is freed, so
    # also count the streams that were freed.
    wait_for_metric(ats, "proxy.process.http2.current_server_streams", 0)
    wait_for_value(lambda: len(destroyed_streams(ats)), len(paths), "the number of origin streams freed")
    assert metric(ats, "proxy.process.http2.stream_errors") == 0


def destroyed_streams(ats: ATS) -> list[str]:
    """Return the http2_stream debug lines for every Http2Stream ATS has freed so far."""
    traffic = ats.traffic_out.read_text(errors="replace")
    return re.findall(r"\(http2_stream\) .* Destroy stream, ", traffic)


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
    """Free an origin stream whose empty END_STREAM arrives after the whole response.

    The origin sends the whole response at once but ends each stream half a second later.
    /late-end-empty has no body, so its transaction releases the stream and finishes before the
    END_STREAM arrives, and nothing else is left to free the stream. A stream left behind would
    time out at the keep-alive timeout as an HTTP/2 stream error and take the pooled origin
    connection down with it. An HTTP/2 origin body normally ends only at END_STREAM, so for /late-end-body
    and /late-end-large the transaction is still reading the body from the stream when the
    END_STREAM arrives, and must finish the body from it. For /late-end-large the client also reads
    only after the END_STREAM, so ATS is still writing the body to it then.

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
    ended = r"^conn \d+ ended stream=\d+ path=/late-end-large$"
    assert get_slowly(ats, "/late-end-large", lambda: origin.wait_for_output(ended)) == (200, LARGE_SPLIT_SIZE)
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


def test_h2_origin_late_empty_end_stream_after_buffered_body(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Keep a late empty END_STREAM away from a transaction that has already read the whole body.

    The hold_txn_mutex plugin holds the transaction's mutex on a task thread while the origin's
    response header and whole body arrive. The header's read event misses its try-lock and is
    retried, so the body is counted on the stream's read VIO first. When the retry runs, HttpSM
    finds the whole body already buffered, ends the server side of the tunnel at once with
    HTTP_TUNNEL_EVENT_PRECOMPLETE and releases the stream, which waits for END_STREAM. The client
    reads only after the END_STREAM arrives, so the transaction is still writing the body to it
    then. The READ_COMPLETE for that END_STREAM must not reach the transaction, which has stopped
    handling events for the stream: its tunnel handler takes it for the end of the tunnel, cuts
    the client off, and fails an assertion in a debug build.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    ats, origin, stop = start_origin(
        ats_factory,
        services,
        plugin="hold_txn_mutex.so held-late-end 20 500",
        **{
            "proxy.config.net.sock_send_buffer_size_in": 2048,
            "proxy.config.diags.debug.tags": "http|http2|hold_txn_mutex",
        },
    )

    # The first request opens the origin connection and settles its SETTINGS.
    paths = ["/ok-before", "/held-late-end"]
    assert get(ats, "/ok-before") == 200
    ended = r"^conn \d+ ended stream=\d+ path=/held-late-end$"
    assert get_slowly(ats, "/held-late-end", lambda: origin.wait_for_output(ended)) == (200, LARGE_SPLIT_SIZE)

    # Check that the test reached the window it is for: the server side of the tunnel was done
    # before the tunnel started, so the stream was released while it waited for END_STREAM.
    traffic = ats.traffic_out.read_text(errors="replace")
    held = re.search(r"\(hold_txn_mutex\) \[(\d+)\] holding the transaction mutex", traffic)
    assert held is not None, "the plugin never held the transaction mutex"
    assert re.search(rf"\(http_tunnel\) \[{held[1]}\] \[tunnel_run\] producer already done", traffic), (
        "the body was not all buffered before the tunnel started"
    )
    assert "[tunnel_handler, VC_EVENT_READ_COMPLETE" not in traffic
    # release() disabled the read VIO, so the stream held the READ_COMPLETE for END_STREAM.
    assert "defer VC_EVENT_READ_COMPLETE until the read VIO is re-enabled" in traffic

    paths.append("/ok-after")
    assert get(ats, "/ok-after") == 200
    assert_clean_origin_session(ats, origin, paths)

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    assert_clean_diags(ats)


def test_h2_origin_end_stream_without_length_while_header_waits(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Finish a response with no content-length whose END_STREAM arrives while its header waits to be read.

    The hold_txn_mutex plugin holds the transaction's mutex on a task thread while the origin sends a
    response header with no content-length, the whole body and an empty END_STREAM DATA frame. The
    header's read event misses its try-lock and is retried, and the READ_COMPLETE for END_STREAM
    takes its place, so the transaction reads the header knowing the origin has ended the stream.
    No more frames will come for the stream, so the transaction must take the buffered body as the
    whole body and not wait for more until the inactivity timeout cuts the client off.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    inactivity_timeout = 2
    ats, origin, stop = start_origin(
        ats_factory,
        services,
        plugin="hold_txn_mutex.so held-late-end 20 500",
        **{
            "proxy.config.http.transaction_no_activity_timeout_out": inactivity_timeout,
            "proxy.config.diags.debug.tags": "http|http2|hold_txn_mutex",
        },
    )

    # The first request opens the origin connection and settles its SETTINGS.
    paths = ["/ok-before", "/held-late-end-nocl"]
    assert get(ats, "/ok-before") == 200
    status, length, seconds = get_timed(ats, "/held-late-end-nocl")
    assert (status, length) == (200, LARGE_SPLIT_SIZE)
    assert seconds < inactivity_timeout - 0.5, f"the response took {seconds:.2f} s"

    # Check that the test reached the window it is for: the header was read on the READ_COMPLETE.
    traffic = ats.traffic_out.read_text(errors="replace")
    held = re.search(r"\(hold_txn_mutex\) \[(\d+)\] holding the transaction mutex", traffic)
    assert held is not None, "the plugin never held the transaction mutex"
    assert re.search(rf"\(http\) \[{held[1]}\] \[state_read_server_response_header, VC_EVENT_READ_COMPLETE", traffic), (
        "the response header was not read on the READ_COMPLETE for END_STREAM"
    )

    paths.append("/ok-after")
    assert get(ats, "/ok-after") == 200
    assert_clean_origin_session(ats, origin, paths)

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    assert_clean_diags(ats)


def test_h2_origin_end_stream_without_length_while_hook_waits(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Finish a response with no content-length whose END_STREAM arrives while a plugin holds the header hook.

    The origin sends a response header with no content-length at once, and the whole body and an
    empty END_STREAM DATA frame while the hold_txn_mutex plugin keeps the transaction at the read
    response header hook. The transaction reads nothing from the stream there, so the stream drops
    the READ_COMPLETE for END_STREAM. When the hook is done, the body is already buffered and no
    more frames will come, so the transaction must take the buffered body as the whole body.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    inactivity_timeout = 2
    ats, origin, stop = start_origin(
        ats_factory,
        services,
        plugin="hold_txn_mutex.so held-body-nocl 0 500 read-response",
        **{
            "proxy.config.http.transaction_no_activity_timeout_out": inactivity_timeout,
            "proxy.config.diags.debug.tags": "http|http2|hold_txn_mutex",
        },
    )

    paths = ["/ok-before", "/held-body-nocl"]
    assert get(ats, "/ok-before") == 200
    status, length, seconds = get_timed(ats, "/held-body-nocl")
    assert (status, length) == (200, LARGE_SPLIT_SIZE)
    assert seconds < inactivity_timeout - 0.5, f"the response took {seconds:.2f} s"

    # Check that the test reached the window it is for: END_STREAM arrived during the hold.
    origin.wait_for_output(r"^conn \d+ ended stream=\d+ path=/held-body-nocl$")
    traffic = ats.traffic_out.read_text(errors="replace")
    held = re.search(r"\(hold_txn_mutex\) \[(\d+)\] holding the transaction mutex", traffic)
    assert held is not None, "the plugin never held the transaction mutex"
    released = traffic.index(f"[{held[1]}] releasing the transaction mutex", held.end())
    assert "Received DATA frame" in traffic[held.end() : released], "the body did not arrive while the plugin held the transaction"

    paths.append("/ok-after")
    assert get(ats, "/ok-after") == 200
    assert_clean_origin_session(ats, origin, paths)

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    assert_clean_diags(ats)


def test_h2_origin_end_stream_before_header_fails_at_once(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Fail a response at once when the origin ends the stream with an empty DATA frame and no header.

    No header will come for the stream, so the transaction must not wait for the inactivity
    timeout.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Runs the origin.
    """
    inactivity_timeout = 2
    ats, origin, stop = start_origin(
        ats_factory, services, **{"proxy.config.http.transaction_no_activity_timeout_out": inactivity_timeout}
    )

    assert get(ats, "/ok-before") == 200
    status, _, seconds = get_timed(ats, "/end-no-header")
    assert status == 502
    assert seconds < inactivity_timeout - 0.5, f"the response took {seconds:.2f} s"
    origin.wait_for_output(r"^conn \d+ ended stream=\d+ path=/end-no-header$")
    assert get(ats, "/ok-after") == 200
    assert metric(ats, "proxy.process.http2.stream_errors") == 0

    finish_origin(services, origin, stop)
    ats.stop()
    assert "Traceback" not in origin.output, origin.output
    error_log = ats.error_log.read_text(errors="replace")
    assert "Server closed connection while reading response header." in error_log, error_log
    assert_clean_diags(ats)
