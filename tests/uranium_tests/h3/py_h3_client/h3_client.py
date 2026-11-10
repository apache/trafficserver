#!/usr/bin/env python3
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may
#  obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Exercise ATS HTTP/3 client-side behavior with aioquic."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import ssl
from dataclasses import dataclass, field
from typing import Callable

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.buffer import encode_uint_var
from aioquic.h3.connection import H3Connection, H3_ALPN, FrameType, StreamType, encode_frame
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, QuicEvent, StreamDataReceived

LARGE_BODY_SIZE = 300000
SEND_BUFFER_BODY_SIZE = 1048576
H3_REQUEST_CANCELLED = 0x10C
LARGE_BODY_SUFFIX = b"000927b "
REUSED_HEADER_VALUE = b"stable-python-qpack-value"


@dataclass
class RequestCase:
    """A single HTTP/3 request/response expectation."""

    name: str
    method: bytes
    path: str
    request_size: int = 0
    response_size: int = 0
    status: int = 200


@dataclass
class ResponseState:
    """Accumulate response headers and data for one HTTP/3 stream."""

    header_blocks: list[list[tuple[bytes, bytes]]] = field(default_factory=list)
    body: bytearray = field(default_factory=bytearray)

    @property
    def status(self) -> int:
        for header_block in reversed(self.header_blocks):
            for name, value in header_block:
                if name == b":status":
                    return int(value)
        raise RuntimeError("response did not contain :status")


class H3ClientProtocol(QuicConnectionProtocol):
    """Minimal HTTP/3 client protocol with raw QUIC stream helpers."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._http = H3Connection(self._quic)
        self._responses: dict[int, ResponseState] = {}
        self._waiters: dict[int, asyncio.Future[ResponseState]] = {}
        self._raw_response_bytes: dict[int, int] = {}
        self._event_counts: dict[str, int] = {}
        self._terminated: asyncio.Future[ConnectionTerminated] = asyncio.get_running_loop().create_future()

    def quic_event_received(self, event: QuicEvent) -> None:
        event_name = type(event).__name__
        self._event_counts[event_name] = self._event_counts.get(event_name, 0) + 1

        if isinstance(event, ConnectionTerminated) and not self._terminated.done():
            self._terminated.set_result(event)
            for waiter in self._waiters.values():
                if not waiter.done():
                    waiter.set_exception(RuntimeError(f"connection terminated: {event.error_code} {event.reason_phrase}"))

        for http_event in self._http.handle_event(event):
            if isinstance(http_event, HeadersReceived):
                response = self._responses.setdefault(http_event.stream_id, ResponseState())
                response.header_blocks.append(http_event.headers)
                if http_event.stream_ended:
                    self._complete_response(http_event.stream_id)
            elif isinstance(http_event, DataReceived):
                response = self._responses.setdefault(http_event.stream_id, ResponseState())
                response.body.extend(http_event.data)
                if http_event.stream_ended:
                    self._complete_response(http_event.stream_id)

        if isinstance(event, StreamDataReceived):
            self._raw_response_bytes[event.stream_id] = self._raw_response_bytes.get(event.stream_id, 0) + len(event.data)
            if event.end_stream and event.stream_id in self._responses:
                self._complete_response(event.stream_id)

        self.transmit()

    def _complete_response(self, stream_id: int) -> None:
        waiter = self._waiters.get(stream_id)
        if waiter is not None and not waiter.done():
            waiter.set_result(self._responses[stream_id])

    async def request(self, authority: str, request_case: RequestCase) -> ResponseState:
        stream_id = self._quic.get_next_available_stream_id()
        waiter: asyncio.Future[ResponseState] = asyncio.get_running_loop().create_future()
        self._waiters[stream_id] = waiter

        headers = [
            (b":method", request_case.method),
            (b":scheme", b"https"),
            (b":authority", authority.encode()),
            (b":path", request_case.path.encode()),
            (b"user-agent", b"ats-h3-aioquic-uranium"),
            (b"x-h3-python-client", b"aioquic"),
            (b"x-h3-reused-header", REUSED_HEADER_VALUE),
            (b"x-h3-test-case", request_case.name.encode()),
            (b"uuid", request_case.name.encode()),
        ]
        if request_case.request_size > 0:
            headers.extend(
                [
                    (b"content-type", b"application/octet-stream"),
                    (b"content-length", str(request_case.request_size).encode()),
                ]
            )

        request_body = generated_body(request_case.request_size)
        self._http.send_headers(stream_id, headers, end_stream=not request_body)
        if request_body:
            self._http.send_data(stream_id, request_body, end_stream=True)
        self.transmit()

        try:
            return await asyncio.wait_for(waiter, timeout=30)
        except TimeoutError as e:
            response = self._responses.get(stream_id)
            raw_response_bytes = self._raw_response_bytes.get(stream_id, 0)
            event_summary = ", ".join(f"{name}={count}" for name, count in sorted(self._event_counts.items()))
            if response is None:
                raise TimeoutError(
                    f"{request_case.name}: timed out before receiving response headers; "
                    f"raw QUIC stream bytes={raw_response_bytes}; "
                    f"events=[{event_summary}]"
                ) from e
            raise TimeoutError(
                f"{request_case.name}: timed out after receiving {len(response.header_blocks)} header block(s) and "
                f"{len(response.body)} response byte(s); raw QUIC stream bytes={raw_response_bytes}; events=[{event_summary}]"
            ) from e

    async def wait_for_body_bytes(self, stream_id: int, size: int) -> None:
        """Wait until at least ``size`` response body bytes arrived on a stream."""
        for _ in range(3000):
            response = self._responses.get(stream_id)
            if response is not None and len(response.body) >= size:
                return
            await asyncio.sleep(0.01)
        raise TimeoutError(f"stream {stream_id}: fewer than {size} response body bytes arrived")

    def stop_sending(self, stream_id: int) -> None:
        """Ask the server to stop sending a response, as a client does when it cancels a request."""
        waiter = self._waiters.pop(stream_id, None)
        if waiter is not None:
            waiter.cancel()
        self._quic.stop_stream(stream_id, H3_REQUEST_CANCELLED)
        self.transmit()

    async def wait_for_termination(self) -> ConnectionTerminated:
        return await asyncio.wait_for(self._terminated, timeout=5)

    def send_unknown_unidirectional_stream(self) -> None:
        stream_id = self._quic.get_next_available_stream_id(is_unidirectional=True)
        self._quic.send_stream_data(stream_id, encode_uint_var(0x21) + b"ignored", end_stream=True)
        self.transmit()

    def send_client_push_stream(self) -> None:
        stream_id = self._quic.get_next_available_stream_id(is_unidirectional=True)
        self._quic.send_stream_data(stream_id, encode_uint_var(StreamType.PUSH) + encode_uint_var(0), end_stream=False)
        self.transmit()

    def send_duplicate_control_stream(self) -> None:
        stream_id = self._quic.get_next_available_stream_id(is_unidirectional=True)
        payload = encode_uint_var(StreamType.CONTROL) + encode_frame(FrameType.SETTINGS, b"")
        self._quic.send_stream_data(stream_id, payload, end_stream=False)
        self.transmit()

    def send_reserved_request_frame(self) -> None:
        stream_id = self._quic.get_next_available_stream_id()
        self._quic.send_stream_data(stream_id, encode_frame(0x21, b""), end_stream=True)
        self.transmit()

    def send_data_before_headers(self) -> None:
        stream_id = self._quic.get_next_available_stream_id()
        self._quic.send_stream_data(stream_id, encode_frame(FrameType.DATA, b"bad"), end_stream=True)
        self.transmit()


def generated_body(size: int) -> bytes:
    """Generate deterministic content matching Proxy Verifier size bodies."""
    chunks: list[bytes] = []
    total = 0
    value = 0
    while total < size:
        chunk = f"{value:07x} ".encode()
        chunks.append(chunk)
        total += len(chunk)
        value += 1
    return b"".join(chunks)[:size]


def quic_configuration(server_name: str, max_data: int | None = None, max_stream_data: int | None = None) -> QuicConfiguration:
    """Create an insecure test-only HTTP/3 client configuration.

    :param max_data: The connection flow control window to advertise, or None for aioquic's default.
    :param max_stream_data: The stream flow control window to advertise, or None for aioquic's default.
    """
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN, server_name=server_name)
    configuration.verify_mode = ssl.CERT_NONE
    if max_data is not None:
        configuration.max_data = max_data
    if max_stream_data is not None:
        configuration.max_stream_data = max_stream_data
    return configuration


async def connect_h3(host: str, port: int, server_name: str, max_data: int | None = None, max_stream_data: int | None = None):
    """Open an HTTP/3 connection using the test protocol."""
    return connect(
        host,
        port,
        configuration=quic_configuration(server_name, max_data=max_data, max_stream_data=max_stream_data),
        create_protocol=H3ClientProtocol,
    )


def verify_response(request_case: RequestCase, response: ResponseState) -> None:
    """Verify one response matches the expected status and body."""
    body = bytes(response.body)
    if response.status != request_case.status:
        raise AssertionError(f"{request_case.name}: expected status {request_case.status}, got {response.status}")

    if request_case.method == b"HEAD" or request_case.status == 204:
        if body:
            raise AssertionError(f"{request_case.name}: expected no response body, got {len(body)} bytes")
        return

    expected = generated_body(request_case.response_size)
    if body != expected:
        raise AssertionError(f"{request_case.name}: response body mismatch: got {len(body)}, expected {len(expected)}")
    if request_case.response_size == LARGE_BODY_SIZE and not body.endswith(LARGE_BODY_SUFFIX):
        raise AssertionError(f"{request_case.name}: large body suffix mismatch")


async def run_requests(host: str, port: int, authority: str, server_name: str, request_cases: list[RequestCase]) -> None:
    """Run a sequence of request cases on one HTTP/3 connection."""
    async with await connect_h3(host, port, server_name) as client:
        for request_case in request_cases:
            response = await client.request(authority, request_case)
            verify_response(request_case, response)
            print(f"ok {request_case.name}")


async def run_concurrent_requests(host: str, port: int, authority: str, server_name: str, request_cases: list[RequestCase]) -> None:
    """Run request cases concurrently on one HTTP/3 connection."""
    async with await connect_h3(host, port, server_name) as client:
        responses = await asyncio.gather(*(client.request(authority, request_case) for request_case in request_cases))
        for request_case, response in zip(request_cases, responses):
            verify_response(request_case, response)
            print(f"ok {request_case.name}")


async def expect_connection_error(
    host: str,
    port: int,
    server_name: str,
    name: str,
    action: Callable[[H3ClientProtocol], None],
) -> None:
    """Run a malformed action and require ATS to close the QUIC connection."""
    async with await connect_h3(host, port, server_name) as client:
        action(client)
        terminated = await client.wait_for_termination()
        if terminated.error_code == 0:
            raise AssertionError(f"{name}: expected non-zero H3/QUIC close error")
        print(f"ok {name} error={terminated.error_code}")


async def run_edge_cases(host: str, port: int, authority: str, server_name: str) -> None:
    """Exercise H3 control stream and frame behavior with raw stream writes."""
    async with await connect_h3(host, port, server_name) as client:
        client.send_unknown_unidirectional_stream()
        request_case = RequestCase("py-edge-after-unknown", b"GET", "/py-edge-after-unknown", response_size=100)
        response = await client.request(authority, request_case)
        verify_response(request_case, response)
        print("ok py-unknown-unidirectional-stream")

    await expect_connection_error(
        host, port, server_name, "py-client-push-stream-rejected", lambda client: client.send_client_push_stream()
    )
    await expect_connection_error(
        host, port, server_name, "py-duplicate-control-stream-rejected", lambda client: client.send_duplicate_control_stream()
    )
    async with await connect_h3(host, port, server_name) as client:
        client.send_reserved_request_frame()
        request_case = RequestCase("py-edge-after-reserved", b"GET", "/py-edge-after-reserved", response_size=100)
        response = await client.request(authority, request_case)
        verify_response(request_case, response)
        print("ok py-reserved-request-frame-ignored")

    await expect_connection_error(
        host, port, server_name, "py-data-before-headers-rejected", lambda client: client.send_data_before_headers()
    )


async def run_send_buffer_windows(host: str, port: int, authority: str, server_name: str, windows: list[int]) -> None:
    """Fetch a 1 MB response through small stream flow control windows, then reuse the connection.

    A small window makes OpenSSL take only part of a write, the last one included, so the
    response's final bytes and its FIN go out on a later write than the one that offered them.
    """
    for window in windows:
        async with await connect_h3(host, port, server_name, max_stream_data=window) as client:
            for request_case in (
                RequestCase(f"send-buffer-window-{window}", b"GET", "/send-buffer", response_size=SEND_BUFFER_BODY_SIZE),
                RequestCase(f"send-buffer-window-{window}-second", b"GET", "/send-buffer", response_size=100),
            ):
                response = await client.request(authority, request_case)
                verify_response(request_case, response)
                print(f"ok {request_case.name}")


async def run_send_buffer_concurrent(
    host: str, port: int, authority: str, server_name: str, streams: int, max_data: int, max_stream_data: int
) -> None:
    """Fetch several 1 MB responses at once on one connection."""
    request_cases = [
        RequestCase(f"send-buffer-concurrent-{index}", b"GET", "/send-buffer", response_size=SEND_BUFFER_BODY_SIZE)
        for index in range(streams)
    ]
    async with await connect_h3(host, port, server_name, max_data=max_data, max_stream_data=max_stream_data) as client:
        responses = await asyncio.gather(*(client.request(authority, request_case) for request_case in request_cases))
        for request_case, response in zip(request_cases, responses):
            verify_response(request_case, response)
            print(f"ok {request_case.name}")


async def run_send_buffer_stop_sending(host: str, port: int, authority: str, server_name: str) -> None:
    """Cancel a 1 MB response partway through, then fetch another on the same connection."""
    async with await connect_h3(host, port, server_name) as client:
        cancelled = RequestCase("send-buffer-stop-sending", b"GET", "/send-buffer", response_size=SEND_BUFFER_BODY_SIZE)
        # request() sends on the next stream ID, which nothing else claims meanwhile.
        stream_id = client._quic.get_next_available_stream_id()
        cancelled_request = asyncio.ensure_future(client.request(authority, cancelled))
        await client.wait_for_body_bytes(stream_id, 64 * 1024)
        client.stop_sending(stream_id)
        with contextlib.suppress(asyncio.CancelledError):
            await cancelled_request
        print(f"stopped {cancelled.name} on stream {stream_id}")

        second = RequestCase("send-buffer-stop-sending-second", b"GET", "/send-buffer", response_size=SEND_BUFFER_BODY_SIZE)
        response = await client.request(authority, second)
        verify_response(second, response)
        print(f"ok {second.name}")


async def async_main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--addr", required=True, help="ATS HTTP/3 address in host:port form")
    parser.add_argument("--authority", required=True, help="HTTP/3 request authority")
    parser.add_argument("--server-name", required=True, help="TLS SNI server name")
    parser.add_argument(
        "--sequential-requests",
        type=int,
        default=0,
        help="Only send this many small GETs, one after another, on one connection",
    )
    parser.add_argument(
        "--scenario",
        choices=("interop", "send-buffer-windows", "send-buffer-concurrent", "send-buffer-stop-sending"),
        default="interop",
        help="Which checks to run",
    )
    parser.add_argument("--max-data", type=int, default=65536, help="Connection flow control window for send-buffer-concurrent")
    parser.add_argument(
        "--max-stream-data",
        type=int,
        action="append",
        help="Stream flow control window; send-buffer-windows takes it more than once",
    )
    parser.add_argument("--streams", type=int, default=8, help="Concurrent requests for send-buffer-concurrent")
    args = parser.parse_args()

    host, port_text = args.addr.rsplit(":", 1)
    port = int(port_text)

    if args.sequential_requests > 0:
        small_get = RequestCase("py-get-small", b"GET", "/py-get-small", response_size=100)
        await run_requests(host, port, args.authority, args.server_name, [small_get] * args.sequential_requests)
        print(f"completed {args.sequential_requests} sequential HTTP/3 requests")
        return
    if args.scenario == "send-buffer-windows":
        await run_send_buffer_windows(host, port, args.authority, args.server_name, args.max_stream_data or [16384])
        print("completed send-buffer-windows")
        return
    if args.scenario == "send-buffer-concurrent":
        max_stream_data = args.max_stream_data[0] if args.max_stream_data else SEND_BUFFER_BODY_SIZE
        await run_send_buffer_concurrent(host, port, args.authority, args.server_name, args.streams, args.max_data, max_stream_data)
        print("completed send-buffer-concurrent")
        return
    if args.scenario == "send-buffer-stop-sending":
        await run_send_buffer_stop_sending(host, port, args.authority, args.server_name)
        print("completed send-buffer-stop-sending")
        return

    sequential_cases = [
        RequestCase("py-get-empty", b"GET", "/py-get-empty"),
        RequestCase("py-get-small", b"GET", "/py-get-small", response_size=100),
        RequestCase("py-head-no-body", b"HEAD", "/py-head-no-body", response_size=100),
        RequestCase("py-204-no-body", b"GET", "/py-204-no-body", status=204),
        RequestCase("py-post-small", b"POST", "/py-post-small", request_size=100, response_size=100),
        RequestCase("py-put-small", b"PUT", "/py-put-small", request_size=100, response_size=100),
        RequestCase("py-delete-empty", b"DELETE", "/py-delete-empty", status=204),
        RequestCase("py-options-small", b"OPTIONS", "/py-options-small", response_size=100),
    ]
    concurrent_cases = [
        RequestCase("py-get-concurrent-large", b"GET", "/py-get-concurrent-large", response_size=LARGE_BODY_SIZE),
        RequestCase("py-get-concurrent-small", b"GET", "/py-get-concurrent-small", response_size=100),
    ]
    large_cases = [
        RequestCase("py-get-large", b"GET", "/py-get-large", response_size=LARGE_BODY_SIZE),
        RequestCase("py-post-large", b"POST", "/py-post-large", request_size=LARGE_BODY_SIZE, response_size=LARGE_BODY_SIZE),
        RequestCase("py-put-large", b"PUT", "/py-put-large", request_size=LARGE_BODY_SIZE, response_size=LARGE_BODY_SIZE),
    ]

    await run_requests(host, port, args.authority, args.server_name, sequential_cases)
    await run_requests(host, port, args.authority, args.server_name, large_cases)
    await run_concurrent_requests(host, port, args.authority, args.server_name, concurrent_cases)
    await run_edge_cases(host, port, args.authority, args.server_name)
    print("completed 18 Python HTTP/3 checks")


def main() -> None:
    asyncio.run(async_main())


if __name__ == "__main__":
    main()
