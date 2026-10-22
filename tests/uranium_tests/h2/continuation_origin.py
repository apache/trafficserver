#!/usr/bin/env python3
"""Serve HTTP/2 responses whose header blocks require CONTINUATION frames."""

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

import argparse
import socket
import ssl
import sys
import time

from origin_lifecycle import OriginLifecycle, send_bytes

from hyperframe.frame import HeadersFrame, ContinuationFrame
from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import DataReceived, RequestReceived, StreamEnded, PingAckReceived, StreamReset

HTTP2_FRAME_HEADER_SIZE = 9
HTTP2_FRAME_TYPE_CONTINUATION = 0x09


def count_frames(payload: bytes, frame_type: int) -> int:
    """Count frames of ``frame_type`` in an HTTP/2 wire payload."""
    count = 0
    offset = 0
    while offset < len(payload):
        if len(payload) - offset < HTTP2_FRAME_HEADER_SIZE:
            raise RuntimeError("truncated HTTP/2 frame header")

        length = int.from_bytes(payload[offset:offset + 3], "big")
        frame_end = offset + HTTP2_FRAME_HEADER_SIZE + length
        if frame_end > len(payload):
            raise RuntimeError("truncated HTTP/2 frame payload")
        if payload[offset + 3] == frame_type:
            count += 1
        offset = frame_end
    return count


def serve_connection(tls_socket: ssl.SSLSocket, expected_responses: int, lifecycle: OriginLifecycle) -> int:
    """Serve requests on one HTTP/2 connection."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    send_bytes(tls_socket, connection.data_to_send())

    responses_sent = 0
    paths: dict[int, str] = {}
    pending_304_ping = False
    ping_deadline = 0.0
    # Do not initiate connection shutdown after sending the expected
    # responses. A large header block spans multiple TLS records, and closing
    # here can race the peer draining the final CONTINUATION and DATA frames.
    # Instead, wait for the explicit stop request after client verification.
    while not lifecycle.stopped or pending_304_ping:
        if pending_304_ping and time.monotonic() > ping_deadline:
            raise AssertionError("ATS did not acknowledge the PING after the empty 304 DATA")
        try:
            data = tls_socket.recv(65535)
        except TimeoutError:
            continue
        if not data:
            return responses_sent

        for event in connection.receive_data(data):
            if isinstance(event, StreamReset) and paths.get(event.stream_id) == "/304-data":
                raise AssertionError(f"ATS reset the valid 304 after empty DATA: {event.error_code}")
            if isinstance(event, PingAckReceived):
                if event.ping_data == b"earlyhdr":
                    lifecycle.stop_file.with_suffix(".early").touch()
                else:
                    assert event.ping_data == b"304check"
                    pending_304_ping = False
                    print("304_empty_data_accepted=1", flush=True)
                continue
            if isinstance(event, RequestReceived):
                paths[event.stream_id] = dict(event.headers)[":path"]
                if paths[event.stream_id] != "/early":
                    continue
            elif isinstance(event, DataReceived):
                connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                continue
            elif isinstance(event, StreamEnded):
                if paths[event.stream_id] == "/early":
                    continue
            else:
                continue
            if paths[event.stream_id] == "/slow":
                # Exceed the initial-header timeout after the full request arrived.
                time.sleep(3)
            if paths[event.stream_id] == "/304-data":
                connection.send_headers(event.stream_id, [(":status", "304"), ("content-length", "123")])
                send_bytes(tls_socket, connection.data_to_send())
                # Force a later read, after ATS has converted the response headers.
                time.sleep(0.1)
                connection.send_data(event.stream_id, b"", end_stream=True)
                connection.ping(b"304check")
                pending_304_ping = True
                ping_deadline = time.monotonic() + 5
                send_bytes(tls_socket, connection.data_to_send())
                responses_sent += 1
            else:
                # Together these values are intentionally larger than the
                # default 16 KiB maximum frame size even after HPACK Huffman
                # encoding. Each field remains below ATS's per-field limit.
                padding_one = f"{event.stream_id:08x}-" + ("0123456789abcdef" * 1024)
                padding_two = f"{event.stream_id:08x}-" + ("fedcba9876543210" * 1024)
                headers = [(":status", "200")]
                if paths[event.stream_id] != "/trailers":
                    headers.extend(
                        [
                            ("content-length", "4"), ("x-continuation-padding-one", padding_one),
                            ("x-continuation-padding-two", padding_two)
                        ])
                connection.send_headers(event.stream_id, headers)
                connection.send_data(event.stream_id, b"okay", end_stream=paths[event.stream_id] != "/trailers")
                if paths[event.stream_id] == "/trailers":
                    send_bytes(tls_socket, connection.data_to_send())
                    connection.send_headers(event.stream_id, [("x-trailer-one", "one"), ("x-trailer-two", "two")], end_stream=True)
                    wire = connection.data_to_send()
                    assert wire[3] == 1 and len(wire) == 9 + int.from_bytes(wire[:3], "big")
                    # Force CONTINUATION without requiring large downstream trailers.
                    head = HeadersFrame(event.stream_id)
                    head.flags.add("END_STREAM")
                    head.data = wire[9:10]
                    continuation = ContinuationFrame(event.stream_id)
                    continuation.flags.add("END_HEADERS")
                    continuation.data = wire[10:]
                    send_bytes(tls_socket, head.serialize() + continuation.serialize())
                    print("sent_trailer_continuation=1", flush=True)
                    responses_sent += 1
                    continue

                if paths[event.stream_id] == "/early":
                    connection.ping(b"earlyhdr")
                wire_bytes = connection.data_to_send()
                continuation_frames = count_frames(wire_bytes, HTTP2_FRAME_TYPE_CONTINUATION)
                if continuation_frames == 0:
                    raise RuntimeError("large response header did not generate a CONTINUATION frame")

                print(
                    f"stream={event.stream_id} sent_continuation_frames={continuation_frames}",
                    flush=True,
                )
                send_bytes(tls_socket, wire_bytes)
                responses_sent += 1
                assert responses_sent <= expected_responses, "Unexpected extra request"

    return responses_sent


def run_server(port: int, certificate: str, private_key: str, expected_responses: int, stop_file: str) -> int:
    """Serve until the client response and header assertions finish."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, private_key)
    context.set_alpn_protocols(["h2"])

    responses_sent = 0
    lifecycle = OriginLifecycle(stop_file)
    with socket.create_server(("127.0.0.1", port)) as listener:
        listener.settimeout(0.2)
        while not lifecycle.stopped:
            try:
                plain_socket, _ = listener.accept()
            except TimeoutError:
                continue
            plain_socket.settimeout(5)
            try:
                tls_socket = context.wrap_socket(plain_socket, server_side=True)
            except (ssl.SSLError, TimeoutError):
                # Ignore readiness probes and peers that never finish TLS.
                plain_socket.close()
                continue
            with tls_socket:
                if tls_socket.selected_alpn_protocol() != "h2":
                    raise RuntimeError("ATS did not negotiate HTTP/2 with the origin")
                tls_socket.settimeout(0.2)
                responses_sent += serve_connection(tls_socket, expected_responses - responses_sent, lifecycle)

    assert responses_sent == expected_responses, f"Expected {expected_responses} responses, got {responses_sent}"
    lifecycle.complete()
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    parser.add_argument("expected_responses", type=int)
    parser.add_argument("stop_file")
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.port, args.certificate, args.private_key, args.expected_responses, args.stop_file)


if __name__ == "__main__":
    sys.exit(main())
