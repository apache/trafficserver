#!/usr/bin/env python3
"""An origin server that records HTTP/1.1 request boundaries.

The server parses each request's framing itself (headers, then a
Content-Length-delimited body) and prints what it received, so a test can verify
that the proxy delivered each request to the origin with its boundaries intact.
"""

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
import signal
import socket
import sys


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address to listen on.")
    parser.add_argument("port", type=int, help="The port to listen on.")
    return parser.parse_args()


def get_listening_socket(address: str, port: int) -> socket.socket:
    """Create a listening socket."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((address, port))
    sock.listen(1)
    return sock


def recv_until(sock: socket.socket, buffer: bytes, delimiter: bytes) -> bytes:
    """Read from the socket until the buffer contains the delimiter.

    :param sock: The socket to read from.
    :param buffer: Bytes already read from the socket.
    :param delimiter: The delimiter to read until.
    :returns: The buffer, guaranteed to contain the delimiter, or all bytes read
        before the socket closed.
    """
    while delimiter not in buffer:
        data = sock.recv(4096)
        if not data:
            break
        buffer += data
    return buffer


def response_for(path: str) -> bytes:
    """Build the origin response for a given request target.

    :param path: The request target.
    :returns: The raw response bytes.
    """
    if path == '/second':
        body = b'second response body\n'
        return (
            b'HTTP/1.1 200 OK\r\n'
            b'X-Origin-Response: second\r\n'
            b'Content-Type: text/plain\r\n'
            b'Content-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
    body = b'first response body\n'
    return (
        b'HTTP/1.1 200 OK\r\n'
        b'X-Origin-Response: first\r\n'
        b'Content-Type: text/plain\r\n'
        b'Content-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)


def handle_connection(sock: socket.socket) -> None:
    """Read and record every request received on a single connection.

    :param sock: The accepted client socket.
    """
    sock.settimeout(5.0)
    buffer = b""
    request_count = 0
    while True:
        try:
            buffer = recv_until(sock, buffer, b'\r\n\r\n')
        except socket.timeout:
            print("Timed out waiting for a request.")
            break
        if b'\r\n\r\n' not in buffer:
            print("Connection closed by peer.")
            break

        header_bytes, _, rest = buffer.partition(b'\r\n\r\n')
        header_text = header_bytes.decode(errors='replace')
        lines = header_text.split('\r\n')
        request_line = lines[0]
        path = request_line.split(' ')[1] if len(request_line.split(' ')) > 1 else ''

        content_length = 0
        for line in lines[1:]:
            name, _, value = line.partition(':')
            if name.strip().lower() == 'content-length':
                try:
                    content_length = int(value.strip())
                except ValueError:
                    content_length = 0

        # Read the body, if any, according to Content-Length.
        body = rest
        timed_out = False
        try:
            while len(body) < content_length:
                data = sock.recv(4096)
                if not data:
                    break
                body += data
        except socket.timeout:
            print("Timed out waiting for the request body.")
            timed_out = True
        if timed_out:
            break
        remainder = body[content_length:]
        body = body[:content_length]

        request_count += 1
        print(f'---- ORIGIN REQUEST {request_count} ----')
        print(f'REQUEST_LINE: {request_line}')
        for line in lines[1:]:
            print(f'HEADER: {line}')
        print(f'BODY_LEN: {len(body)}')
        print(f'BODY: {body!r}')
        print(f'---- END ORIGIN REQUEST {request_count} ----')
        print(f'ORIGIN_REQUEST_COUNT: {request_count}')
        sys.stdout.flush()

        sock.sendall(response_for(path))

        # Any bytes past this request's body belong to the next pipelined
        # request on the connection.
        buffer = remainder

    print(f'TOTAL_ORIGIN_REQUESTS: {request_count}')
    sys.stdout.flush()


def main() -> int:
    """Run the recording origin server until terminated by the test harness."""
    # AuTest terminates long-running processes with SIGINT. Register the handler
    # explicitly because SIGINT may be inherited as ignored from the launcher.
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    args = parse_args()
    try:
        with get_listening_socket(args.address, args.port) as listening_sock:
            print(f"Listening on {args.address}:{args.port}")
            sys.stdout.flush()
            while True:
                conn, _ = listening_sock.accept()
                with conn:
                    handle_connection(conn)
    except (KeyboardInterrupt, SystemExit):
        # SIGTERM from the test harness or a Ctrl-C is a clean shutdown.
        pass
    except OSError as e:
        # An unexpected socket error (e.g. bind or accept failure) should be
        # surfaced with a non-zero exit rather than masked as success.
        print(f"Origin server error: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
