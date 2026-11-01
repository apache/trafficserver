#!/usr/bin/env python3
"""A server that replies with a 103 Early Hints response."""

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
from http_utils import (wait_for_headers_complete, determine_outstanding_bytes_to_read, drain_socket)
import socket
import sys
import time


def parse_args() -> argparse.Namespace:
    '''Parse command line arguments.
    :return: The parsed command line arguments.
    '''
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address to listen on")
    parser.add_argument("port", type=int, default=8080, help="The port to listen on")
    parser.add_argument("--num-103", type=int, default=2, help="Number of 103 responses to send before the 200 OK")
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


def send_responses(sock: socket.socket, num_103_responses: int) -> None:
    """Send an HTTP response.

    :param sock: The socket to write to.
    :param num_103_responses: The number of 103 responses to send before the
    200 OK.
    """
    for _ in range(num_103_responses):
        response = ('HTTP/1.1 103 Early Hints\r\n'
                    'Link: </style.css>; rel=preload\r\n\r\n')
        print(f'Sending:\n{response}')
        sock.sendall(response.encode("utf-8"))
        time.sleep(0.1)
    response = ('HTTP/1.1 200 OK\r\n'
                'Content-Length: 10\r\n\r\n')
    print(f'Sending:\n{response}')
    sock.sendall(response.encode("utf-8"))
    time.sleep(0.1)
    body = b'10bytebody'
    print(f'Sending body:\n{body.decode()}')
    sock.sendall(body)


def main() -> int:
    '''Start the server that replies with 103 respones.
    :return: The exit status of the server.
    '''
    args = parse_args()

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

                send_responses(sock, args.num_103)
    return 0


if __name__ == "__main__":
    sys.exit(main())
