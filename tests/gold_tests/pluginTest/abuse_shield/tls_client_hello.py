#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more contributor license
# agreements. See the NOTICE file distributed with this work for additional information regarding
# copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
"""Send a deterministic TLS ClientHello for fingerprint blocking tests."""

import argparse
import socket
import struct


def extension(extension_type: int, data: bytes) -> bytes:
    """Encode a TLS extension."""
    return struct.pack("!HH", extension_type, len(data)) + data


def client_hello(server_name: str) -> bytes:
    """Build the ClientHello whose JA3 source is CLIENT_JA3_SOURCE."""
    encoded_name = server_name.encode("ascii")
    server_name_list = b"\x00" + struct.pack("!H", len(encoded_name)) + encoded_name
    extensions = b"".join(
        (
            extension(0,
                      struct.pack("!H", len(server_name_list)) + server_name_list),
            extension(10, b"\x00\x02\x00\x1d"),
            extension(11, b"\x01\x00"),
            extension(13, b"\x00\x04\x08\x04\x04\x03"),
            extension(43, b"\x02\x03\x03"),
        ))

    body = b"".join(
        (
            b"\x03\x03",
            bytes(range(32)),
            b"\x00",
            b"\x00\x02\xc0\x2f",
            b"\x01\x00",
            struct.pack("!H", len(extensions)),
            extensions,
        ))
    handshake = b"\x01" + len(body).to_bytes(3, byteorder="big") + body
    return b"\x16\x03\x01" + struct.pack("!H", len(handshake)) + handshake


def main() -> int:
    """Send the deterministic ClientHello and check the TLS response type."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--expect", required=True, choices=("response", "reject"))
    args = parser.parse_args()

    received = b""
    try:
        with socket.create_connection((args.host, args.port), timeout=5) as connection:
            connection.settimeout(5)
            connection.sendall(client_hello("example.test"))
            received = connection.recv(4096)
    except (ConnectionResetError, BrokenPipeError):
        received = b""
    except socket.timeout:
        print("Timed out waiting for ATS to finish the handshake or reject it")
        return 1

    if args.expect == "response":
        if not received:
            print("ATS closed without a TLS response")
            return 1
        if received[0] != 0x16:
            print(f"ATS returned TLS record type {received[0]}, not a handshake")
            return 1
        print(f"ATS returned {len(received)} TLS handshake bytes")
        return 0

    if received and received[0] != 0x15:
        print(f"ATS unexpectedly returned TLS record type {received[0]}")
        return 1
    if received:
        print("ATS rejected the ClientHello with a TLS alert before ServerHello")
    else:
        print("ATS rejected the ClientHello by closing before ServerHello")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
