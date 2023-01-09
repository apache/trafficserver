#!/usr/bin/env python3
"""Implements a client which slowly POSTs a request."""

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
    """Parse the command line arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "proxy_address",
        help="Address of the proxy to connect to.")
    parser.add_argument(
        "proxy_port",
        type=int,
        help="The port of the proxy to connect to.")
    parser.add_argument(
        '-s', '--server-hostname',
        dest="server_hostname",
        default="some.server.com",
        help="The hostname of the server to connect to.")
    parser.add_argument(
        "-t", "--send_time",
        dest="send_time",
        type=int,
        default=3,
        help="The number of seconds to send the POST.")
    parser.add_argument(
        '--finish-request',
        dest="finish_request",
        action='store_true',
        help="Finish sending the request before closing the connection.")

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


def send_slow_post(
        sock: socket.socket,
        server_hostname: str,
        send_time: int,
        finish_request: bool) -> None:
    """Send a slow POST request.

    :param sock: The socket to send the request on.
    :param server_hostname: The hostname of the server to connect to.
    :param send_time: The number of seconds to send the request.
    :param finish_request: Whether to finish sending the request before closing
    the connection.
    """
    # Send the POST request.
    host_header = f'Host: {server_hostname}\r\n'.encode()
    request = (
        b"POST / HTTP/1.1\r\n"
        + host_header +
        b"Transfer-Encoding: chunked\r\n"
        b"\r\n")
    sock.sendall(request)
    print('Sent request headers:')
    print(request.decode())

    print(f'Sending POST body for {send_time} seconds.')
    counter = 0
    while counter < send_time:
        # Send zero padded hex string of the counter.
        chunk = f'8\r\n{counter:08x}\r\n'.encode()
        sock.sendall(chunk)
        print(f'Sent chunk: {chunk.decode()}')
        counter += 1

    if finish_request:
        # Send the last chunk.
        sock.sendall(b'0\r\n\r\n')
    else:
        print('Aborting the request before sending the last chunk.')
        sock.close()


def drain_response(sock: socket.socket) -> None:
    """Drain the response from the server.

    :param sock: The socket to read the response from.
    """
    print('Waiting for the response to complete.')
    read_bytes = wait_for_headers_complete(sock)
    num_bytes_to_drain = determine_outstanding_bytes_to_read(read_bytes)
    drain_socket(sock, read_bytes, num_bytes_to_drain)
    print('Response complete.')


def main() -> int:
    """Run the client."""
    args = parse_args()
    print(args)

    with open_connection(args.proxy_address, args.proxy_port) as sock:
        send_slow_post(
            sock,
            args.server_hostname,
            args.send_time,
            args.finish_request)

        if args.finish_request:
            drain_response(sock)
    print('Done.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
