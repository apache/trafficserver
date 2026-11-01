#!/usr/bin/env python3
#
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
"""Issue a raw HTTP/1 request and reject trailers after the terminal chunk."""

from __future__ import annotations

import socket
import sys

TERMINAL_CHUNK = b"0\r\n\r\n"


def send_request_and_read_response(host: str, port: int) -> bytes:
    """Return the raw bytes from a single HTTP/1 response."""
    request = (
        b"GET /trailers HTTP/1.1\r\n"
        b"Host: example.data.com\r\n"
        b"uuid: h2-origin-trailers-h1\r\n"
        b"Connection: keep-alive\r\n"
        b"\r\n")
    response = bytearray()
    saw_terminal_chunk = False

    with socket.create_connection((host, port), timeout=5) as conn:
        conn.settimeout(5)
        conn.sendall(request)
        while True:
            try:
                chunk = conn.recv(4096)
            except socket.timeout:
                if saw_terminal_chunk:
                    break
                raise
            if not chunk:
                break
            response.extend(chunk)
            if TERMINAL_CHUNK in response:
                saw_terminal_chunk = True
                conn.settimeout(0.5)

    return bytes(response)


def main() -> int:
    """Verify ATS does not append H2 origin trailers to an HTTP/1 response."""
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <host> <port>", file=sys.stderr)
        return 2

    response = send_request_and_read_response(sys.argv[1], int(sys.argv[2]))
    terminal_chunk_index = response.find(TERMINAL_CHUNK)
    trailer_index = response.lower().find(b"x-ats-h2-trailer")

    print(response.decode("utf-8", errors="replace"))

    if b"hello from h2 origin" not in response:
        print("Did not receive the expected response body.", file=sys.stderr)
        return 1
    if terminal_chunk_index == -1:
        print("Did not receive an HTTP/1 chunked terminal marker.", file=sys.stderr)
        return 1
    if trailer_index != -1:
        print("H2 origin trailer was forwarded to the HTTP/1 client.", file=sys.stderr)
        return 1

    print("No H2 origin trailers were forwarded to the HTTP/1 client.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
