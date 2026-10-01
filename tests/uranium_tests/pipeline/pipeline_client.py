#!/usr/bin/env python3
"""A client that sends three pipelined GET requests."""

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


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("proxy_address", metavar="proxy-address", help="Address of the proxy to connect to.")
    parser.add_argument("proxy_port", metavar="proxy-port", type=int, help="The port of the proxy to connect to.")
    parser.add_argument('first_hostname', metavar='first-hostname', help='The Host header field value of the first request.')
    parser.add_argument('second_hostname', metavar='second-hostname', help='The Host header field value of the second request.')
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


def write_pipelined_requests(sock: socket.socket, first_hostname: str, second_hostname: str) -> None:
    """Write three pipelined requests to the socket.

    :param sock: The socket to write to.
    :param first_hostname: The Host header field value of the first request.
    :param second_hostname: The Host header field value of the second request.
    """
    first_request = f'GET /first HTTP/1.1\r\nHost: {first_hostname}\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n'
    # For if we want to test CL first. Leave this out of the final commit.
    #first_request = f'GET /first HTTP/1.1\r\nHost: {first_hostname}\r\nContent-Length: 5\r\n\r\n67891'
    second_request = f'GET /second HTTP/1.1\r\nHost: {first_hostname}\r\nContent-Length: 5\r\n\r\n12345'
    third_request = f'DELETE /third HTTP/1.1\r\nHost: {second_hostname}\r\nContent-Length: 0\r\n\r\n'
    pipelined_requests = first_request + second_request + third_request
    total = len(first_request) + len(second_request) + len(third_request)
    print(
        f'Sending three pipelined requests: {len(first_request)} bytes, '
        f'{len(second_request)} bytes, and {len(third_request)} bytes: '
        f'{total} total bytes')
    print(pipelined_requests)
    sock.sendall(pipelined_requests.encode())
    print()


def wait_for_responses(sock: socket.socket, num_responses: int) -> bytes:
    """Wait for the responses to be complete.

    :param sock: The socket to read from.
    :param num_responses: The number of responses to wait for.
    :returns: The bytes read off the socket.
    """
    responses = b""
    while True:
        data = sock.recv(1024)
        if not data:
            print("Socket closed.")
            break
        print(f'Received:\n{data}')
        responses += data
        if responses.count(b"\r\n\r\n") == num_responses:
            break
    return responses


def main() -> int:
    """Send the pipelined requests."""
    args = parse_args()
    with open_connection(args.proxy_address, args.proxy_port) as s:
        write_pipelined_requests(s, args.first_hostname, args.second_hostname)
        print("Waiting for responses...")
        responses = wait_for_responses(s, 3)
    print()
    print(f'Received responses:\n{responses.decode()}')
    return 0


if __name__ == "__main__":
    sys.exit(main())
