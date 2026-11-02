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
import time


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
    parser.add_argument(
        '--mode',
        choices=["continue", "immediate", "delayed", "chunked"],
        default="continue",
        help="Whether to verify plain 100-Continue handling, or to probe body framing with a body that "
        "accompanies the headers, follows them, or is chunked.")
    parser.add_argument('--path', default="/api/1", help="The request target to use.")
    parser.add_argument('--delay', type=float, default=1.0, help="Seconds to wait before sending a delayed body.")
    parser.add_argument('--read-timeout', type=float, default=2.0, help="Seconds to wait for further proxy output.")
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


def build_framing_probe(server_hostname: str, path: str, mode: str) -> tuple:
    """Build a POST whose body is itself a well formed request.

    If the proxy does not account for the body when the origin answers early,
    the body is framed as a second request and the client sees two responses to
    its single request.

    :param server_hostname: The hostname of the server to connect to.
    :param path: The request target to use.
    :param mode: One of immediate, delayed or chunked.
    :return: The request headers and the body, as they should appear on the wire.
    """
    smuggled: bytes = (f'GET /smuggled HTTP/1.1\r\n'
                       f'Host: {server_hostname}\r\n'
                       f'X-Smuggled: yes\r\n'
                       f'\r\n').encode()
    if mode == 'chunked':
        body = f'{len(smuggled):x}\r\n'.encode() + smuggled + b'\r\n0\r\n\r\n'
        framing = 'Transfer-Encoding: chunked\r\n'
    else:
        body = smuggled
        framing = f'Content-Length: {len(body)}\r\n'

    headers: bytes = (
        f'POST {path} HTTP/1.1\r\n'
        f'Host: {server_hostname}\r\n'
        f'Connection: keep-alive\r\n'
        f'Expect: 100-continue\r\n'
        f'{framing}'
        f'\r\n').encode()
    return headers, body


def read_all_output(sock: socket.socket, read_timeout: float) -> bytes:
    """Read until the proxy closes the connection or stops sending.

    The drain helpers in http_utils assume a single response whose length is
    known from its headers. Here the whole point is to detect an unexpected
    second response, so everything is read and counted afterwards instead.

    :param sock: The socket to read from.
    :param read_timeout: Seconds of silence after which reading stops.
    :return: Every byte the proxy sent.
    """
    sock.settimeout(read_timeout)
    received = b''
    while True:
        try:
            data = sock.recv(4096)
        except TimeoutError:
            print('Proxy went idle.')
            break
        if not data:
            print('Proxy closed the connection.')
            break
        received += data
    return received


def count_final_responses(received: bytes) -> int:
    """Count non-interim responses in the proxy's output.

    A 1xx response is informational and does not conclude a request, so it is
    not counted.

    :param received: Every byte the proxy sent.
    :return: The number of final responses observed.
    """
    return sum(1 for line in received.split(b'\r\n') if line.startswith(b'HTTP/1.') and not line.startswith(b'HTTP/1.1 1'))


def probe_body_framing(sock: socket.socket, args: argparse.Namespace) -> None:
    """Send the framing probe and report what the proxy replied.

    :param sock: The socket to send the request on.
    :param args: The parsed command line arguments.
    """
    headers, body = build_framing_probe(args.server_hostname, args.path, args.mode)
    if args.mode == 'immediate':
        # A single write, so the body is already buffered by the proxy when the
        # origin answers. This is the only case a drain can ever satisfy.
        sock.sendall(headers + body)
        print(f'Sent headers and body together (mode={args.mode}).')
    else:
        sock.sendall(headers)
        # Let the origin answer before the body reaches the proxy, so the body is
        # not sitting in the proxy's read buffer when the transaction finishes.
        time.sleep(args.delay)
        sock.sendall(body)
        print(f'Sent body after a delay (mode={args.mode}).')

    received = read_all_output(sock, args.read_timeout)
    print(f'----- received -----\n{received.decode(errors="replace")}\n--------------------')
    print(f'RESPONSES_RECEIVED={count_final_responses(received)}')
    print(f'CONNECTION_CLOSE_HEADER={"yes" if b"connection: close" in received.lower() else "no"}')


def main() -> int:
    """Run the client."""
    args = parse_args()
    print(args)

    with open_connection(args.proxy_address, args.proxy_port) as sock:
        if args.mode == 'continue':
            send_expect_request(sock, args.server_hostname)
            drain_response(sock)
        else:
            probe_body_framing(sock, args)
    print('Done.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
