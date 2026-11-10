#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""How much response data ATS hands OpenSSL's QUIC stream send buffers."""

from __future__ import annotations

import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import pytest
from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

H3_PY_CLIENT = Path(__file__).parent / "py_h3_client" / "h3_client.py"

H3_LARGE_RESPONSE__replay = "replays/h3_large_response.replay.yaml"
H3_SEND_BUFFER_ORIGIN__replay = "replays/h3_send_buffer_origin.replay.yaml"

# Matches OpenSSL's INIT_APP_BUF_LEN, the size it creates each stream's send buffer at.
# OpenSSL grows the buffer only when one write offers more data than it has free.
OPENSSL_INITIAL_STREAM_SEND_BUFFER = 8192

MAX_STREAM_SEND_BUFFER = 1048576
# Below what eight 1 MB streams would hold at the per-stream limit.
MAX_CONNECTION_SEND_BUFFER = 2 * 1048576
# ATS offers no more than the room left under each limit and OpenSSL takes no more than it
# is offered, so the logged totals should stay within the limits. The slack keeps the checks
# about whether a limit holds rather than about exact byte counts.
LIMIT_SLACK = 16 * 1024

# Each response fits under the stream limit, so ATS can hand OpenSSL all of it and finish the
# transaction; eight of them are twice the connection limit.
UNACKNOWLEDGED_BODY_SIZE = 524288
UNACKNOWLEDGED_STREAMS = 8
# How many of those responses must have gone into OpenSSL whole. The connection limit holds
# four, so three is a margin for a slow run, and still more than any one stream in flight.
WHOLE_UNACKNOWLEDGED_RESPONSES = 3

SEND_BUFFER_BODY_SIZE = 1048576
# A quarter of the response, so a stream that ignored the limit would show it clearly.
SMALL_STREAM_SEND_BUFFER = 262144

# Small enough that a few 1 MB responses fill it many times over.
SHARED_CONNECTION_SEND_BUFFER = 262144
SHARED_STREAMS = 4

CANNOT_WRITE_LINE = r"stream {stream}: cannot write, the sending side is reset or the connection is closed"

WRITE_LINE = re.compile(
    r"\[(?P<cid>[^]]+)\] stream (?P<stream>[0-9]+): OpenSSL holds (?P<used>[0-9]+) bytes to send or to be acknowledged "
    r"in a (?P<size>[0-9]+)-byte buffer, (?P<connection>[0-9]+) for the connection"
)


@dataclass
class StreamWrite:
    """What one v_quic_net write line says OpenSSL holds after the write."""

    stream: tuple[str, int]
    used: int
    size: int
    connection: int


def stream_writes(content: str) -> list[StreamWrite]:
    """Parse every write line in traffic.out, in order."""
    return [
        StreamWrite((m["cid"], int(m["stream"])), int(m["used"]), int(m["size"]), int(m["connection"]))
        for m in WRITE_LINE.finditer(content)
    ]


def grown_streams(writes: list[StreamWrite]) -> dict[tuple[str, int], tuple[int, int]]:
    """Map each stream whose buffer outgrew the size first logged for it to (first size, largest size)."""
    first: dict[tuple[str, int], int] = {}
    largest: dict[tuple[str, int], int] = {}
    for write in writes:
        first.setdefault(write.stream, write.size)
        largest[write.stream] = max(largest.get(write.stream, 0), write.size)
    return {stream: (first[stream], largest[stream]) for stream in first if largest[stream] > first[stream]}


def read_settled(path: Path, timeout: float = 15, line: re.Pattern[str] = WRITE_LINE) -> str:
    """Read traffic.out once ATS has stopped adding lines that match ``line`` to it."""
    deadline = time.monotonic() + timeout
    content = path.read_text(errors="replace")
    count = len(line.findall(content))
    while time.monotonic() < deadline:
        time.sleep(1)
        content = path.read_text(errors="replace")
        latest = len(line.findall(content))
        if latest == count:
            return content
        count = latest
    return content


def wait_for_growth(path: Path, timeout: float = 15) -> list[StreamWrite]:
    """Wait for a write line showing a stream's buffer larger than the size first logged for it."""
    deadline = time.monotonic() + timeout
    writes: list[StreamWrite] = []
    while time.monotonic() < deadline:
        if path.exists():
            writes = stream_writes(path.read_text(errors="replace"))
            if grown_streams(writes):
                return writes
        time.sleep(0.2)
    sizes = sorted({write.size for write in writes})
    raise AssertionError(
        f"No stream's OpenSSL send buffer grew past the size first logged for it in {len(writes)} writes; "
        f"buffer sizes seen: {sizes} (OpenSSL starts at {OPENSSL_INITIAL_STREAM_SEND_BUFFER})"
    )


def require_openssl_quic(ats_factory: ATSFactory) -> None:
    """Skip unless ATS uses the OpenSSL QUIC backend, the only one these limits apply to."""
    if not (ats_factory.has_feature("TS_USE_QUIC") and ats_factory.has_feature("TS_HAS_OPENSSL_QUIC")):
        pytest.skip("ATS was built without the OpenSSL QUIC backend")


def configure_ats(
    ats_factory: ATSFactory,
    name: str,
    server: VerifierServer,
    remap: str,
    max_stream_send_buffer: int = MAX_STREAM_SEND_BUFFER,
    max_connection_send_buffer: int = MAX_CONNECTION_SEND_BUFFER,
) -> ATS:
    """Configure HTTP/3 with quic_net debugging on, so each stream write logs what OpenSSL holds.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique Traffic Server name within the scenario.
    :param server: The origin.
    :param remap: The remap rule's source URL.
    :param max_stream_send_buffer: The per-stream send limit.
    :param max_connection_send_buffer: The per-connection send limit.
    """

    ats = ats_factory.create(name, enable_tls=True, enable_quic=True, enable_cache=False)
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic_net|v_quic_net",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            "proxy.config.quic.max_stream_send_buffer": max_stream_send_buffer,
            "proxy.config.quic.max_connection_send_buffer": max_connection_send_buffer,
        }
    )
    ats.remap_config.add_line(f"map {remap} http://127.0.0.1:{server.http_port}/")
    return ats


def aioquic_client(services: ServiceFactory, ats: ATS, scenario: str, *options: str) -> ProcessService:
    """Create an aioquic client that runs one send buffer scenario against ATS.

    :param services: Factory owning support services and their cleanup.
    :param ats: The Traffic Server under test.
    :param scenario: The h3_client.py scenario to run.
    :param options: Further h3_client.py arguments.
    """

    return services.process(
        f"client-{scenario}",
        (
            sys.executable,
            H3_PY_CLIENT,
            "--addr",
            f"127.0.0.1:{ats.https_port}",
            "--authority",
            f"py.example.com:{ats.https_port}",
            "--server-name",
            "py.example.com",
            "--scenario",
            scenario,
            *options,
        ),
    )


def run_aioquic_scenario(
    ats_factory: ATSFactory, services: ServiceFactory, scenario: str, *options: str, **limits: int
) -> tuple[ATS, str]:
    """Start an origin and ATS, run one aioquic scenario to completion, and return ATS and the client's output.

    :param limits: configure_ats() send limit arguments.
    """
    server = services.verifier_server(
        f"server-{scenario}", H3_SEND_BUFFER_ORIGIN__replay, verbose=False, other_args="--poll-timeout 30000"
    )
    ats = configure_ats(ats_factory, f"ts-{scenario}", server, "/", **limits)
    client = aioquic_client(services, ats, scenario, *options)

    server.start()
    ats.start()
    result = client.run(timeout=120)
    assert result.returncode == 0, result.output
    assert f"completed {scenario}" in result.stdout, result.output
    return ats, result.stdout


def test_h3_stream_send_buffer_grows(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS offers a stream more than OpenSSL's initial send buffer holds, so OpenSSL grows the buffer, up to the stream limit.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    server = services.verifier_server(
        "server-h3-stream-send-buffer", H3_LARGE_RESPONSE__replay, verbose=False, other_args="--poll-timeout 30000"
    )
    ats = configure_ats(ats_factory, "ts-h3-stream-send-buffer", server, "https://example.com/")
    client = services.verifier_client(
        "client-h3-stream-send-buffer",
        H3_LARGE_RESPONSE__replay,
        http3_ports=[ats.https_port],
        verbose=False,
        poll_timeout=30000,
    )

    server.start()
    ats.start()
    result = client.run(timeout=120)
    assert result.returncode == 0, result.output

    wait_for_growth(ats.traffic_out)
    writes = stream_writes(read_settled(ats.traffic_out))
    most_held = max(write.used for write in writes)
    assert most_held <= MAX_STREAM_SEND_BUFFER + LIMIT_SLACK, (
        f"OpenSSL held {most_held} bytes for one stream, more than the {MAX_STREAM_SEND_BUFFER}-byte stream limit"
    )


def test_h3_stream_send_buffer_small_flow_control_windows(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A 1 MB response arrives intact through stream windows smaller than what ATS offers, and the connection stays usable.

    OpenSSL takes only part of a write that goes past the stream's flow control window, so
    the response's last bytes and its FIN leave on a later write than the one that offered them.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    _, stdout = run_aioquic_scenario(
        ats_factory, services, "send-buffer-windows", "--max-stream-data", "16384", "--max-stream-data", "10000"
    )
    for window in (16384, 10000):
        assert f"ok send-buffer-window-{window}\n" in stdout
        assert f"ok send-buffer-window-{window}-second\n" in stdout


def test_h3_stream_send_buffer_connection_limit(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Concurrent 1 MB responses on one connection arrive intact, and OpenSSL never holds more than the connection limit for them.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    ats, stdout = run_aioquic_scenario(
        ats_factory,
        services,
        "send-buffer-concurrent",
        "--streams",
        "8",
        "--max-data",
        "65536",
        "--max-stream-data",
        str(MAX_STREAM_SEND_BUFFER),
    )
    assert len(re.findall(r"^ok send-buffer-concurrent-[0-9]+$", stdout, re.MULTILINE)) == 8

    writes = stream_writes(read_settled(ats.traffic_out))
    assert len({write.stream for write in writes}) >= 8, "expected write lines for all eight response streams"
    most_held = max(write.connection for write in writes)
    assert most_held <= MAX_CONNECTION_SEND_BUFFER + LIMIT_SLACK, (
        f"OpenSSL held {most_held} bytes for the connection, more than the {MAX_CONNECTION_SEND_BUFFER}-byte connection limit"
    )


def test_h3_stream_send_buffer_stream_limit(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """OpenSSL never holds more than the stream limit for one stream, even when the response and every window are larger.

    The client acknowledges nothing, so OpenSSL keeps all that ATS hands it, and the stream
    flow control window and the connection limit both exceed the response. Only the stream
    limit stops ATS from handing over the whole response.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    ats, stdout = run_aioquic_scenario(
        ats_factory, services, "send-buffer-stream-limit", max_stream_send_buffer=SMALL_STREAM_SEND_BUFFER
    )
    assert "sent 1 requests" in stdout, stdout

    writes = stream_writes(read_settled(ats.traffic_out))
    assert writes, "expected write lines for the response stream"
    most_held = max(write.used for write in writes)
    assert most_held <= SMALL_STREAM_SEND_BUFFER + LIMIT_SLACK, (
        f"OpenSSL held {most_held} bytes for one stream, more than the {SMALL_STREAM_SEND_BUFFER}-byte stream limit"
    )
    assert most_held >= SMALL_STREAM_SEND_BUFFER - LIMIT_SLACK, (
        f"OpenSSL held at most {most_held} bytes for the stream, so the {SMALL_STREAM_SEND_BUFFER}-byte stream limit was never reached"
    )


def test_h3_stream_send_buffer_connection_limit_is_shared(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Once the connection limit binds, every stream with data gets a share of the room that frees, a later one included.

    The client grows its connection flow control window by a little at a time, so data leaves
    OpenSSL much more slowly than ATS can hand it over and OpenSSL frees room only as each
    grant is used and acknowledged. If that room always went to the same stream, the large
    responses would arrive one after another and the small one would wait behind them.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    _, stdout = run_aioquic_scenario(
        ats_factory,
        services,
        "send-buffer-shared",
        "--streams",
        str(SHARED_STREAMS),
        max_connection_send_buffer=SHARED_CONNECTION_SEND_BUFFER,
    )
    assert "ok send-buffer-shared-late\n" in stdout, stdout
    assert len(re.findall(r"^ok send-buffer-shared-[0-9]+$", stdout, re.MULTILINE)) == SHARED_STREAMS, stdout

    def large_bytes(when: str) -> dict[int, int]:
        match = re.search(rf"^when {when}.* had ((?:[0-9]+=[0-9]+ ?)+)$", stdout, re.MULTILINE)
        assert match, stdout
        return {int(stream): int(size) for stream, size in (pair.split("=") for pair in match[1].split())}

    at_late = large_bytes("the small response completed")
    assert all(size < SEND_BUFFER_BODY_SIZE for size in at_late.values()), (
        f"the small response completed only after a large one had: body bytes per large stream {at_late}"
    )

    first = re.search(r"^when the first large response completed, on stream ([0-9]+),", stdout, re.MULTILINE)
    assert first, stdout
    others = {stream: size for stream, size in large_bytes("the first large response completed").items() if stream != int(first[1])}
    assert min(others.values()) >= SEND_BUFFER_BODY_SIZE // 2, (
        f"when stream {first[1]} completed, another large response had less than half of its body: {others}"
    )


def test_h3_stream_send_buffer_stop_sending(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """After a client cancels a 1 MB response partway through, ATS stops writing to that stream and the connection carries the next request.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    ats, stdout = run_aioquic_scenario(ats_factory, services, "send-buffer-stop-sending")
    assert "ok send-buffer-stop-sending-second\n" in stdout

    match = re.search(r"^stopped send-buffer-stop-sending on stream ([0-9]+)$", stdout, re.MULTILINE)
    assert match, stdout
    stream_id = match[1]

    # OpenSSL resets the sending side when STOP_SENDING arrives. ATS can be partway through a
    # write when OpenSSL processes it, so one write may fail; after that ATS sees the reset and
    # leaves the stream alone.
    cannot_write = re.compile(CANNOT_WRITE_LINE.format(stream=stream_id))
    wait_for_file_lines(ats.traffic_out, cannot_write.pattern, 1)
    content = read_settled(ats.traffic_out, line=cannot_write)
    failed_writes = re.findall(rf"Failed to write [0-9]+ bytes on QUIC stream {stream_id}:", content)
    assert len(failed_writes) <= 1, f"ATS kept writing to cancelled stream {stream_id}: {len(failed_writes)} failed writes"
    resets_seen = len(cannot_write.findall(content))
    assert resets_seen == 1, f"ATS went back to cancelled stream {stream_id} after seeing the reset: {resets_seen} times"


def test_h3_stream_send_buffer_connection_limit_counts_finished_streams(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A finished response still counts toward the connection limit while OpenSSL holds it unacknowledged.

    The client acknowledges no response data, so OpenSSL keeps everything ATS hands it, and
    what OpenSSL reports holding for each stream only grows. Their sum is what OpenSSL holds
    for the connection, whatever ATS's own running total says.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    require_openssl_quic(ats_factory)
    ats, stdout = run_aioquic_scenario(
        ats_factory, services, "send-buffer-unacknowledged", "--streams", str(UNACKNOWLEDGED_STREAMS)
    )
    assert f"sent {UNACKNOWLEDGED_STREAMS} requests" in stdout, stdout

    held: dict[tuple[str, int], int] = {}
    for write in stream_writes(read_settled(ats.traffic_out)):
        held[write.stream] = max(held.get(write.stream, 0), write.used)
    whole = sum(1 for size in held.values() if size >= UNACKNOWLEDGED_BODY_SIZE)
    assert whole >= WHOLE_UNACKNOWLEDGED_RESPONSES, (
        f"only {whole} streams got their whole {UNACKNOWLEDGED_BODY_SIZE}-byte response into OpenSSL, fewer than "
        f"{WHOLE_UNACKNOWLEDGED_RESPONSES}, so too few finished transactions were counted; held per stream: {sorted(held.values())}"
    )
    total = sum(held.values())
    assert total <= MAX_CONNECTION_SEND_BUFFER + LIMIT_SLACK, (
        f"OpenSSL holds {total} unacknowledged bytes across {len(held)} streams, more than the "
        f"{MAX_CONNECTION_SEND_BUFFER}-byte connection limit"
    )
