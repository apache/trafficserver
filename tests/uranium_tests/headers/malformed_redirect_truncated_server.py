#!/usr/bin/env python3
"""An origin that writes raw responses, most stopping mid header block."""

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
import sys

LOCATION = b'Location: http://www.example.com/elsewhere\r\n'

# Most of these responses stop before the blank line that ends the header block.
# The origin closes instead, which is the one way a header block can end that a
# replay origin cannot produce, and the only parse outcome with nothing left
# unread behind it. The last two entries do send a complete message. They are
# here for the other reason this file reads the socket directly, which is to pin
# what happens to the connection a refused response arrived on.
RESPONSES = {
    # The fields that arrived are all there is. This is the shape a genuinely
    # broken origin produces, and it must still get its redirect through.
    '/truncated-302': (b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Content-Length: 0\r\n'),
    # The same early close, but the fields that did arrive disagree about the
    # body length. Running out of header does not make the framing trustworthy.
    '/truncated-conflicting-cl-302': (b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Content-Length: 5\r\nContent-Length: 11\r\n'),
    # Transfer-Encoding supersedes Content-Length, so the body boundary is
    # unambiguous once Content-Length is dropped.
    '/truncated-te-cl-302': (b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Transfer-Encoding: chunked\r\nContent-Length: 5\r\n'),
    # Two Content-Length fields that agree. They mean one body length, so
    # exactly one of them should survive.
    '/truncated-dup-cl-302': (b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Content-Length: 5\r\nContent-Length: 5\r\n'),
    # Ambiguous framing on a connection the origin leaves open, so a proxy that
    # returned this connection to its pool would carry the unread remainder of
    # the body into whatever response came next on it.
    '/pooled-conflicting-cl-302': (
        b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Content-Length: 5\r\nContent-Length: 11\r\n\r\nhello world'
    ),
    # A redirect with nothing wrong with it, used to show the connection the
    # refused response arrived on did not poison the next one.
    '/well-formed-302': (b'HTTP/1.1 302 Found\r\n' + LOCATION + b'Content-Length: 5\r\nConnection: close\r\n\r\nhello'),
}

# Paths whose response deliberately leaves the connection open, so the test can
# tell whether the proxy reuses a connection that still has unread bytes on it.
KEEP_ALIVE_PATHS = frozenset({'/pooled-conflicting-cl-302'})

NOT_FOUND = b'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments.

    :returns: The parsed arguments.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('address', help='Address to listen on.')
    parser.add_argument('port', type=int, help='The port to listen on.')
    return parser.parse_args()


def get_listening_socket(address: str, port: int) -> socket.socket:
    """Create a listening socket.

    :param address: The address to listen on.
    :param port: The port to listen on.
    :returns: A listening socket.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((address, port))
    sock.listen(5)
    return sock


def read_request(sock: socket.socket) -> bytes:
    """Read a request header block from the socket.

    :param sock: The socket to read from.
    :returns: The bytes of the request through the end of the header block, or
        an empty bytes object if the peer sent nothing before going away.
    """
    request = b''
    try:
        while b'\r\n\r\n' not in request:
            chunk = sock.recv(4096)
            if not chunk:
                break
            request += chunk
    except (TimeoutError, OSError):
        return b''
    return request


def select_response(request: bytes) -> tuple:
    """Pick the canned response that matches the request path.

    :param request: The raw request bytes.
    :returns: A tuple of the raw response bytes and whether the connection
        should be left open afterwards.
    """
    request_line = request.split(b'\r\n', 1)[0].decode('ascii', errors='replace')
    fields = request_line.split()
    path = fields[1] if len(fields) > 1 else ''
    print(f'Received request line: {request_line}')
    sys.stdout.flush()
    return RESPONSES.get(path, NOT_FOUND), path in KEEP_ALIVE_PATHS


def serve_connection(sock: socket.socket) -> None:
    """Answer requests on one connection until it is done.

    :param sock: The accepted connection.
    """
    sock.settimeout(10)
    while True:
        request = read_request(sock)
        if not request:
            # Most likely the autest port check, which connects and closes
            # without sending anything.
            print('No request on this connection.')
            sys.stdout.flush()
            return
        response, keep_alive = select_response(request)
        sock.sendall(response)
        if not keep_alive:
            return


def run_server(address: str, port: int) -> None:
    """Serve canned responses until interrupted.

    :param address: The address to listen on.
    :param port: The port to listen on.
    """
    with get_listening_socket(address, port) as listening_sock:
        while True:
            with listening_sock.accept()[0] as sock:
                serve_connection(sock)


def main() -> int:
    """Run the origin.

    :returns: 0 on a clean shutdown.
    """
    args = parse_args()
    try:
        run_server(args.address, args.port)
    except KeyboardInterrupt:
        print('Handling KeyboardInterrupt.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
