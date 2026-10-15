#!/usr/bin/env python3
"""Implements a client which tests Expect: 100-Continue."""

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

from http_utils import (wait_for_headers_complete, determine_outstanding_bytes_to_read, drain_socket)

import argparse
import socket
import sys


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments.

    :return: The parsed arguments.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("proxy_address", help="Address of the proxy to connect to.")
    parser.add_argument("proxy_port", type=int, help="The port of the proxy to connect to.")
    parser.add_argument(
        '-s',
        '--server-hostname',
        dest="server_hostname",
        default="some.server.com",
        help="The hostname of the server to connect to.")
    return parser.parse_args()


def open_connection(address: str, port: int) -> socket.socket:
    """Open a connection to the desired host.

    :param address: The address of the host to connect to.
    :param port: The port of the host to connect to.
    :return: The socket connected to the host.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((address, port))
    print(f'Connected to {address}:{port}')
    return sock


def send_expect_request(sock: socket.socket, server_hostname: str) -> None:
    """Send an Expect: 100-Continue request.

    :param sock: The socket to send the request on.
    :param server_hostname: The hostname of the server to connect to.
    """
    # Send the POST request.
    host_header: bytes = f'Host: {server_hostname}\r\n'.encode()
    request: bytes = (
        b"GET /api/1 HTTP/1.1\r\n" + host_header + b"Connection: keep-alive\r\n"
        b"Content-Length: 3\r\n"
        b"uuid: expect\r\n"
        b"Expect: 100-Continue\r\n"
        b"\r\n")
    sock.sendall(request)
    print('Sent Expect: 100-Continue request:')
    print(request.decode())
    drain_response(sock)
    print('Got 100-Continue response.')
    sock.sendall(b'rst')
    print('Sent "rst" body.')


def drain_response(sock: socket.socket) -> None:
    """Drain the response from the server.

    :param sock: The socket to read the response from.
    """
    print('Waiting for the response to complete.')
    read_bytes: bytes = wait_for_headers_complete(sock)
    try:
        num_bytes_to_drain: int = determine_outstanding_bytes_to_read(read_bytes)
    except ValueError:
        print('No CL found')
        return
    if num_bytes_to_drain > 0:
        drain_socket(sock, read_bytes, num_bytes_to_drain)
    print('Response complete.')


def main() -> int:
    """Run the client."""
    args = parse_args()
    print(args)

    with open_connection(args.proxy_address, args.proxy_port) as sock:
        send_expect_request(sock, args.server_hostname)
        drain_response(sock)
    print('Done.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
