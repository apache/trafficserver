#!/usr/bin/env python3
"""A server that receives possibly pipelined requests."""

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
import time
"""A flag indicating whether all three requests have been received."""
received_third_request: bool = False


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments.

    :returns: The parsed arguments.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address to listen on.")
    parser.add_argument("port", type=int, help="The port to listen on.")
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


def receive_requests(sock: socket.socket) -> None:
    """Receive three requests from the client.

    :param sock: The socket to read from.
    """
    global received_third_request

    all_received_data: bytes = b""
    this_request: bytes = b""
    first_response_bytes: bytes = b'HTTP/1.1 200 OK\r\nX-Response: first\r\nContent-Length: 0\r\n\r\n'
    second_response_bytes: bytes = b'HTTP/1.1 200 OK\r\nX-Response: second\r\nContent-Length: 0\r\n\r\n'
    third_response_bytes: bytes = b'HTTP/1.1 200 OK\r\nX-Response: third\r\nContent-Length: 0\r\n\r\n'
    processing_first_request: bool = True
    processing_second_request: bool = False
    processing_third_request: bool = False

    # Note that this is very ad-hoc. We expect the first request to be chunked,
    # the second to have a Content-Length body, and the third, if we receive it,
    # to have no body.
    end_of_first_request: bytes = b'\r\n0\r\n\r\n'
    #end_of_first_request: bytes = b'67891' # < revert this eventually.
    end_of_second_request: bytes = b'12345'
    end_of_third_request: bytes = b'\r\n\r\n'
    while not received_third_request:
        data = sock.recv(1024)
        if not data:
            print("Socket closed.")
            break
        print(f'Received:')
        print(data)
        this_request += data
        all_received_data += data
        while not received_third_request:
            if processing_first_request:
                end_of_request_index = this_request.find(end_of_first_request)
                if end_of_request_index == -1:
                    # Need more data.
                    break
                print('  Received the first request:')
                print(f'  {this_request[:end_of_request_index + len(end_of_first_request)]}')
                processing_first_request = False
                processing_second_request = True
                # Remove the first request from the buffer.
                this_request = this_request[end_of_request_index + len(end_of_first_request):]
                print('  Sending response to the first request:')
                print(f'  {first_response_bytes}')
                print()
                time.sleep(0.01)
                sock.sendall(first_response_bytes)
                continue

            elif processing_second_request:
                end_of_request_index = this_request.find(end_of_second_request)
                if end_of_request_index == -1:
                    # Need more data.
                    break
                print('  Received the second request:')
                print(f'  {this_request[:end_of_request_index + len(end_of_second_request)]}')
                processing_second_request = False
                processing_third_request = True
                # Remove the second request from the buffer.
                this_request = this_request[end_of_request_index + len(end_of_second_request):]
                print('  Sending response to the second request:')
                print(f'  {second_response_bytes}')
                print()
                time.sleep(0.01)
                sock.sendall(second_response_bytes)
                continue

            elif processing_third_request:
                end_of_request_index = this_request.find(end_of_third_request)
                if end_of_request_index == -1:
                    # Need more data.
                    break
                print('  Received the third request:')
                print(f'  {this_request[:end_of_request_index + len(end_of_third_request)]}')
                processing_third_request = False
                # Remove the third request from the buffer.
                this_request = this_request[end_of_request_index + len(end_of_third_request):]
                print('  Sending response to the third request:')
                print(f'  {third_response_bytes}')
                print()
                time.sleep(0.01)
                sock.sendall(third_response_bytes)
                received_third_request = True
                break
    return all_received_data


def _run_server_inside_try(address: str, port: int) -> int:
    """Run the server to handle the pipelined requests.

    :param address: The address to listen on.
    :param port: The port to listen on.
    :return: 0 on success, 1 on failure (appropriate for the command line return
    code).
    """
    with get_listening_socket(address, port) as listening_sock:
        print(f"Listening on {address}:{port}")

        read_bytes: bytes = b""
        while len(read_bytes) == 0:
            print('Waiting for a connection.')
            with accept_connection(listening_sock) as sock:
                read_bytes = receive_requests(sock)
                if len(read_bytes) == 0:
                    # This is probably the PortOpenv4 test. Try again.
                    print("No bytes read on this connection. Trying again.")
                    sock.close()
                    continue


def run_server(address: str, port: int) -> int:
    """Run the server with exception handling.

    :param address: The address to listen on.
    :param port: The port to listen on.
    :return: 1 if the third request was received (this is bad, we expect it to
    be denied), 0 if it wasn't.
    """

    try:
        ret = _run_server_inside_try(address, port)
    except KeyboardInterrupt:
        print('Handling KeyboardInterrupt.')

    return 1 if received_third_request else 0


def main() -> int:
    """Receive pipelined requests."""
    args = parse_args()
    ret = run_server(args.address, args.port)

    print(f'Done. Third request was received: {received_third_request}')
    return ret


if __name__ == "__main__":
    sys.exit(main())
