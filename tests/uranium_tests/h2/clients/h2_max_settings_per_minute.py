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
"""Exercise the HTTP/2 max_settings_per_minute guard.

This client sends one SETTINGS frame containing two parameters. The matching
AuTest config disables the per-frame settings limit and sets
max_settings_per_minute to one, so ATS should reject the connection with
ENHANCE_YOUR_CALM.
"""

import argparse
import socket
import ssl
import sys

H2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

TYPE_SETTINGS = 0x04
TYPE_GOAWAY = 0x07

FLAG_ACK = 0x01

SETTINGS_HEADER_TABLE_SIZE = 0x01
SETTINGS_MAX_CONCURRENT_STREAMS = 0x03

ENHANCE_YOUR_CALM = 0x0B


def make_frame(frame_type: int, flags: int, stream_id: int, payload: bytes = b"") -> bytes:
    return len(payload).to_bytes(3, "big") + bytes([frame_type, flags]) + (stream_id & 0x7FFFFFFF).to_bytes(4, "big") + payload


def make_setting(setting_id: int, value: int) -> bytes:
    return setting_id.to_bytes(2, "big") + value.to_bytes(4, "big")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: socket.socket):
    header = recv_exact(sock, 9)
    if len(header) == 0:
        return None
    if len(header) != 9:
        raise RuntimeError(f"incomplete frame header: got {len(header)} bytes")

    length = int.from_bytes(header[0:3], "big")
    payload = recv_exact(sock, length)
    if len(payload) != length:
        raise RuntimeError(f"incomplete frame payload: expected {length}, got {len(payload)}")

    return {
        "length": length,
        "type": header[3],
        "flags": header[4],
        "stream_id": int.from_bytes(header[5:9], "big") & 0x7FFFFFFF,
        "payload": payload,
    }


def connect_socket(port: int) -> socket.socket:
    socket.setdefaulttimeout(5)

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(["h2"])

    tls_socket = socket.create_connection(("127.0.0.1", port))
    tls_socket = ctx.wrap_socket(tls_socket, server_hostname="localhost")
    if tls_socket.selected_alpn_protocol() != "h2":
        raise RuntimeError(f"failed to negotiate h2, got {tls_socket.selected_alpn_protocol()!r}")
    return tls_socket


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="TLS port to connect to")
    args = parser.parse_args()

    settings_payload = make_setting(SETTINGS_HEADER_TABLE_SIZE, 4096) + make_setting(SETTINGS_MAX_CONCURRENT_STREAMS, 100)

    tls_socket = connect_socket(args.port)
    try:
        tls_socket.sendall(H2_PREFACE)
        tls_socket.sendall(make_frame(TYPE_SETTINGS, 0, 0, settings_payload))

        while True:
            frame = read_frame(tls_socket)
            if frame is None:
                print("Connection closed before receiving GOAWAY", file=sys.stderr)
                return 1

            frame_type = frame["type"]
            if frame_type == TYPE_SETTINGS and not (frame["flags"] & FLAG_ACK):
                tls_socket.sendall(make_frame(TYPE_SETTINGS, FLAG_ACK, 0))
                continue

            if frame_type == TYPE_GOAWAY:
                error_code = int.from_bytes(frame["payload"][4:8], "big")
                print(f"Received GOAWAY with error code {error_code}")
                return 0 if error_code == ENHANCE_YOUR_CALM else 1
    except socket.timeout:
        print("Timed out waiting for max_settings_per_minute GOAWAY", file=sys.stderr)
        return 1
    finally:
        tls_socket.close()


if __name__ == "__main__":
    raise SystemExit(main())
