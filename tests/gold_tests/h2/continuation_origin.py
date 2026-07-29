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

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import DataReceived, StreamEnded

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


def serve_connection(tls_socket: ssl.SSLSocket, expected_responses: int) -> int:
    """Serve requests on one HTTP/2 connection."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    tls_socket.sendall(connection.data_to_send())

    responses_sent = 0
    # Do not initiate connection shutdown after sending the expected
    # responses. A large header block spans multiple TLS records, and closing
    # here can race the peer draining the final CONTINUATION and DATA frames.
    # Instead, keep servicing the connection until ATS closes it (or AuTest
    # terminates this process during cleanup).
    while True:
        data = tls_socket.recv(65535)
        if not data:
            return responses_sent

        for event in connection.receive_data(data):
            if isinstance(event, DataReceived):
                connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
            elif isinstance(event, StreamEnded):
                # Together these values are intentionally larger than the
                # default 16 KiB maximum frame size even after HPACK Huffman
                # encoding. Each field remains below ATS's per-field limit.
                padding_one = f"{event.stream_id:08x}-" + ("0123456789abcdef" * 1024)
                padding_two = f"{event.stream_id:08x}-" + ("fedcba9876543210" * 1024)
                connection.send_headers(
                    event.stream_id,
                    [
                        (":status", "200"),
                        ("content-length", "4"),
                        ("x-continuation-padding-one", padding_one),
                        ("x-continuation-padding-two", padding_two),
                    ],
                )
                connection.send_data(event.stream_id, b"okay", end_stream=True)

                wire_bytes = connection.data_to_send()
                continuation_frames = count_frames(wire_bytes, HTTP2_FRAME_TYPE_CONTINUATION)
                if continuation_frames == 0:
                    raise RuntimeError("large response header did not generate a CONTINUATION frame")

                print(
                    f"stream={event.stream_id} sent_continuation_frames={continuation_frames}",
                    flush=True,
                )
                tls_socket.sendall(wire_bytes)
                responses_sent += 1


def run_server(port: int, certificate: str, private_key: str, expected_responses: int) -> int:
    """Accept TLS connections until ATS closes after the expected responses."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, private_key)
    context.set_alpn_protocols(["h2"])

    responses_sent = 0
    with socket.create_server(("127.0.0.1", port)) as listener:
        listener.settimeout(30)
        while responses_sent < expected_responses:
            plain_socket, _ = listener.accept()
            try:
                with context.wrap_socket(plain_socket, server_side=True) as tls_socket:
                    if tls_socket.selected_alpn_protocol() != "h2":
                        raise RuntimeError("ATS did not negotiate HTTP/2 with the origin")
                    responses_sent += serve_connection(tls_socket, expected_responses - responses_sent)
            except ssl.SSLError:
                # AuTest's readiness probe opens and closes a plain TCP socket.
                plain_socket.close()

    return 0 if responses_sent == expected_responses else 1


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    parser.add_argument("expected_responses", type=int)
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.port, args.certificate, args.private_key, args.expected_responses)


if __name__ == "__main__":
    sys.exit(main())
