#!/usr/bin/env python3
"""Serve one raw HTTP request for the chunked HTTP/2 AuTest."""

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
from pathlib import Path
import socket
import sys
import time


def parse_args() -> argparse.Namespace:
    """Parse the command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address on which to listen.")
    parser.add_argument("port", type=int, help="Port on which to listen.")
    parser.add_argument("output", type=Path, help="File in which to record the request.")
    parser.add_argument("response", choices=("delayed-chunked", "content-length", "chunked"), help="Response to send.")
    return parser.parse_args()


def make_listening_socket(address: str, port: int) -> socket.socket:
    """Create and return a listening TCP socket."""
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((address, port))
    listener.listen(1)
    return listener


def receive_request(conn: socket.socket) -> bytes:
    """Receive one HTTP request, including its declared body."""
    request = b""
    while b"\r\n\r\n" not in request:
        data = conn.recv(4096)
        if not data:
            return request
        request += data

    header, _, body = request.partition(b"\r\n\r\n")
    content_length = 0
    is_chunked = False
    for field in header.split(b"\r\n")[1:]:
        name, separator, value = field.partition(b":")
        if not separator:
            continue
        name = name.strip().lower()
        value = value.strip().lower()
        if name == b"content-length":
            content_length = int(value)
        elif name == b"transfer-encoding" and b"chunked" in value:
            is_chunked = True

    if is_chunked:
        while not (body.startswith(b"0\r\n\r\n") or b"\r\n0\r\n\r\n" in body):
            data = conn.recv(4096)
            if not data:
                break
            body += data
    else:
        while len(body) < content_length:
            data = conn.recv(4096)
            if not data:
                break
            body += data

    return header + b"\r\n\r\n" + body


def send_response(conn: socket.socket, response: str) -> None:
    """Send the selected raw HTTP response."""
    if response == "delayed-chunked":
        conn.sendall(b"HTTP/1.1 200\r\nTransfer-encoding: chunked\r\n\r\n")
        conn.sendall(b"F\r\n123456789012345\r\n")
        time.sleep(1)
        conn.sendall(b"0\r\n\r\n")
    elif response == "content-length":
        conn.sendall(b"HTTP/1.1 200\r\nContent-length: 15\r\n\r\n123456789012345")
    else:
        conn.sendall(b"HTTP/1.1 200\r\nTransfer-encoding: chunked\r\n\r\nF\r\n123456789012345\r\n0\r\n\r\n")


def main() -> int:
    """Ignore readiness probes, serve one request, and exit."""
    args = parse_args()
    with make_listening_socket(args.address, args.port) as listener:
        while True:
            conn, _ = listener.accept()
            with conn:
                request = receive_request(conn)
                if not request:
                    # When.PortOpen probes the listener without sending data.
                    continue
                args.output.write_bytes(request)
                send_response(conn, args.response)
                return 0


if __name__ == "__main__":
    sys.exit(main())
