#!/usr/bin/env python3

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
"""Send an HTTP/2 request after an extension-rich SETTINGS frame."""

import argparse
import socket
import ssl
import sys
from typing import Tuple

import hpack

H2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

TYPE_DATA = 0x00
TYPE_HEADERS = 0x01
TYPE_RST_STREAM = 0x03
TYPE_SETTINGS = 0x04
TYPE_GOAWAY = 0x07

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04


def make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b"") -> bytes:
    return len(payload).to_bytes(3, "big") + bytes([frame_type, flags]) + (stream_id & 0x7FFFFFFF).to_bytes(4, "big") + payload


def make_setting(setting_id: int, value: int) -> bytes:
    return setting_id.to_bytes(2, "big") + value.to_bytes(4, "big")


def make_socket(port: int) -> ssl.SSLSocket:
    socket.setdefaulttimeout(5)

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(["h2"])

    raw_socket = socket.create_connection(("127.0.0.1", port))
    tls_socket = ctx.wrap_socket(raw_socket, server_hostname="localhost")
    if tls_socket.selected_alpn_protocol() != "h2":
        raise RuntimeError(f"failed to negotiate h2, got {tls_socket.selected_alpn_protocol()!r}")
    return tls_socket


def recv_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError("socket closed")
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: ssl.SSLSocket) -> Tuple[int, int, int, bytes]:
    header = recv_exact(sock, 9)
    length = int.from_bytes(header[0:3], "big")
    frame_type = header[3]
    flags = header[4]
    stream_id = int.from_bytes(header[5:9], "big") & 0x7FFFFFFF
    return frame_type, flags, stream_id, recv_exact(sock, length)


def make_settings_payload() -> bytes:
    settings = [
        (0x01, 4096),
        (0x02, 0),
        (0x05, 16384),
        (0x06, 131072),
        (0x03, 100),
        (0x04, 65535),
        (0x2B61, 65535),
        (0x2B62, 65535),
        (0x2B63, 65535),
        (0x2B64, 10),
        (0x2B65, 10),
    ]
    return b"".join(make_setting(setting_id, value) for setting_id, value in settings)


def run(port: int) -> int:
    encoder = hpack.Encoder()
    decoder = hpack.Decoder()
    request_headers = encoder.encode(
        [
            (":method", "GET"),
            (":scheme", "https"),
            (":authority", "www.example.com"),
            (":path", "/"),
        ])

    with make_socket(port) as sock:
        sock.sendall(H2_PREFACE)
        sock.sendall(make_frame(TYPE_SETTINGS, payload=make_settings_payload()))
        sock.sendall(make_frame(TYPE_HEADERS, FLAG_END_HEADERS | FLAG_END_STREAM, 1, request_headers))

        response_status = None
        try:
            while True:
                frame_type, flags, stream_id, payload = read_frame(sock)
                if frame_type == TYPE_SETTINGS and not (flags & FLAG_ACK):
                    sock.sendall(make_frame(TYPE_SETTINGS, FLAG_ACK))
                    continue
                if frame_type == TYPE_GOAWAY:
                    error_code = int.from_bytes(payload[4:8], "big")
                    print(f"Received GOAWAY with error code {error_code}", file=sys.stderr)
                    return 1
                if frame_type == TYPE_RST_STREAM and stream_id == 1:
                    error_code = int.from_bytes(payload[0:4], "big")
                    print(f"Received RST_STREAM with error code {error_code}", file=sys.stderr)
                    return 1
                if frame_type == TYPE_HEADERS and stream_id == 1:
                    if not (flags & FLAG_END_HEADERS):
                        print("Received an unexpected CONTINUATION sequence", file=sys.stderr)
                        return 1
                    response_headers = decoder.decode(payload)
                    response_status = dict(response_headers).get(":status")
                if stream_id == 1 and frame_type in (TYPE_HEADERS, TYPE_DATA) and (flags & FLAG_END_STREAM):
                    if response_status == "200":
                        print("Received 200 response")
                        return 0
                    print(f"Received response status {response_status!r}", file=sys.stderr)
                    return 1
        except (EOFError, socket.timeout) as exc:
            print(f"Connection ended before the response completed: {exc}", file=sys.stderr)
            return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="ATS TLS port")
    args = parser.parse_args()
    return run(args.port)


if __name__ == "__main__":
    raise SystemExit(main())
