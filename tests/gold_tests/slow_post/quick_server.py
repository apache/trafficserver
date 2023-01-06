#!/usr/bin/env python3
"""A server that replies without waiting for the entire request."""

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

from http_utils import (wait_for_headers_complete,
                        determine_outstanding_bytes_to_read,
                        drain_socket)

import argparse
import socket
import sys


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "address",
        help="Address to listen on")
    parser.add_argument(
        "port",
        type=int,
        default=8080,
        help="The port to listen on")
    parser.add_argument(
        '--drain-request',
        action='store_true',
        help="Drain the entire request before closing the connection")
    parser.add_argument(
        '--abort-response-headers',
        action='store_true',
        help="Abort the response in the midst of sending the response headers")
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
    sock.listen(1)
    return sock


def accept_connection(sock: socket.socket) -> socket.socket:
    """Accept a connection.

    :param sock: The socket to accept a connection on.
    :returns: The accepted socket.
    """
    return sock.accept()[0]


def send_response(sock: socket.socket, abort_early: bool) -> None:
    """Send an HTTP response.

    :param sock: The socket to write to.
    :param abort_early: If True, abort the response before sending the body.
    """
    if abort_early:
        response = "HTTP/1."
    else:
        response = (
            r"HTTP/1.1 200 OK\r\n"
            r"Content-Length: 0\r\n"
            r"\r\n"
        )
    print(f'Sending:\n{response}')
    sock.sendall(response.encode("utf-8"))


def main() -> int:
    """Run the server."""
    args = parse_args()

    # Configure a listening socket on args.address and args.port.
    with get_listening_socket(args.address, args.port) as listening_sock:
        print(f"Listening on {args.address}:{args.port}")

        read_bytes: bytes = b""
        while len(read_bytes) == 0:
            with accept_connection(listening_sock) as sock:
                read_bytes = wait_for_headers_complete(sock)
                if len(read_bytes) == 0:
                    # This is probably the PortOpenv4 test. Try again.
                    print("No bytes read on this connection. Trying again.")
                    sock.close()
                    continue

                # Send a response now, before headers are read. This implements
                # the "quick" attribute of this quick_server.
                send_response(sock, args.abort_response_headers)

                if args.abort_response_headers:
                    # We're done.
                    break

                if args.drain_request:
                    num_bytes_to_drain = determine_outstanding_bytes_to_read(
                        read_bytes)
                    print(f'Read {len(read_bytes)} bytes. '
                          f'Draining {num_bytes_to_drain} bytes.')
                    drain_socket(sock, read_bytes, num_bytes_to_drain)
                else:
                    print(f'Read {len(read_bytes)} bytes. '
                          f'Not draining per configuration.')
    return 0


if __name__ == "__main__":
    sys.exit(main())
