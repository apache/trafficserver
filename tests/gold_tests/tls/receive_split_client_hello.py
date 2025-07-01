#!/usr/bin/env python3
"""Receive a split CLIENT_HELLO over TCP, send a dummy response, then echo all
further data."""

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

# See split_client_hello.py for the original CLIENT_HELLO packet.
EXPECTED_CLIENT_HELLO_LENGTH = 1993


def parse_args() -> argparse.Namespace:
    '''Parse command line arguments.
    :returns: Parsed command line arguments.
    '''
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address to listen on")
    parser.add_argument("port", type=int, help="Port to listen on")
    return parser.parse_args()


def get_listening_socket(address: str, port: int) -> socket.socket:
    '''Create a socket that listens for incoming connections.
    :param address: The address to listen on.
    :param port: The port to listen on.
    :returns: A listening socket.
    '''
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setblocking(True)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((address, port))
    sock.listen(1)
    return sock


def accept_connection(listener: socket.socket) -> socket.socket:
    '''Accept a connection on the given listener socket.

    This will block until a connection comes in and is accepted.

    :param listener: The socket to accept a connection on.
    :returns: The accepted socket.
    '''
    return listener.accept()[0]


def handle_client_connection(listener: socket.socket) -> int:
    '''Handle a client connection by reading data and echoing it back.

    :param listener: The socket listening for client connections.
    :returns: 0 on success, non-zero on error.
    '''
    while True:
        with accept_connection(listener) as conn:
            conn.setblocking(True)
            print(f'Accepted connection from {conn.getpeername()}')
            try:
                data = conn.recv(65536)
            except socket.error as e:
                print(f"Socket error on recv: {e}")
                break
            if not data:
                # This is probably the PortOpenv4 test. Try again.
                print("No bytes read on this connection. Trying again.")
                continue
            print(f"Received CLIENT_HELLO packet of {len(data)} bytes, sending dummy response.")
            if len(data) != EXPECTED_CLIENT_HELLO_LENGTH:
                print(f'Incorrect CLIENT_HELLO length: {len(data)} bytes, expected {EXPECTED_CLIENT_HELLO_LENGTH} bytes.')
                return 1
            try:
                conn.sendall(b"dummy SERVER_HELLO")
            except socket.error as e:
                print(f"Failed to send dummy SERVER_HELLO response: {e}")
                break
            # Echo loop.
            while True:
                try:
                    chunk = conn.recv(65536)
                except socket.error:
                    break
                if not chunk:
                    break
                print(f'Received chunk of {len(chunk)} bytes, echoing back:')
                print(chunk)
                conn.sendall(chunk)
            # Done with this client connection.
            print(f"Client {conn.getpeername()} disconnected. Successfully handled connection.")
            return 0
    return 1


def main() -> int:
    args = parse_args()
    with get_listening_socket(args.address, args.port) as listener:
        print(f"Listening on {args.address}:{args.port}")
        while True:
            try:
                ret = handle_client_connection(listener)
                if ret != 0:
                    print(f"Error handling client connection")
                    return ret
            except KeyboardInterrupt:
                print("Server shutting down.")
                break
            except Exception as e:
                print(f"An error occurred: {e}")
                continue
    return 0


if __name__ == "__main__":
    sys.exit(main())
