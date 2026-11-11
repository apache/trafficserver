#!/usr/bin/env python3
'''
HTTP/2 client that sends empty DATA frame with END_STREAM flag
'''
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

import socket
import ssl

import h2.connection
import h2.events

import argparse


# TODO: cleanup with other HTTP/2 clients (h2active_timeout.py and h2client.py)
def get_socket(port: int) -> socket.socket:
    """Create a TLS-wrapped socket.

    :param port: The port to connect to.

    :returns: A TLS-wrapped socket.
    """

    SERVER_NAME = 'localhost'
    SERVER_PORT = port

    # generic socket and ssl configuration
    socket.setdefaulttimeout(15)

    # Configure an ssl client side context which will not check the server's certificate.
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])

    # open a socket to the server and initiate TLS/SSL
    tls_socket = socket.create_connection((SERVER_NAME, SERVER_PORT))
    tls_socket = ctx.wrap_socket(tls_socket, server_hostname=SERVER_NAME)
    return tls_socket


def make_request(port: int, path: str, n: int) -> None:
    """Establish an HTTP/2 connection and send a request.

    :param port: The port to connect to.
    :param path: The path to request.
    :param n: Number of streams to open.
    """

    tls_socket = get_socket(port)

    h2_connection = h2.connection.H2Connection()
    h2_connection.initiate_connection()
    tls_socket.sendall(h2_connection.data_to_send())

    headers = [
        (':method', 'GET'),
        (':path', path),
        (':authority', 'localhost'),
        (':scheme', 'https'),
    ]

    for stream_id in range(1, n * 2, 2):
        h2_connection.send_headers(stream_id, headers, end_stream=False)
        h2_connection.send_data(stream_id, b'', end_stream=True)

    tls_socket.sendall(h2_connection.data_to_send())

    # keep reading from server
    terminated = False
    error_code = 0
    while not terminated:
        # read raw data from the socket
        data = tls_socket.recv(65536 * 1024)
        if not data:
            break

        # feed raw data into h2, and process resulting events
        events = h2_connection.receive_data(data)
        for event in events:
            print(event)
            if isinstance(event, h2.events.ConnectionTerminated):
                if not event.error_code == 0:
                    error_code = event.error_code
                terminated = True

        # send any pending data to the server
        tls_socket.sendall(h2_connection.data_to_send())

    # tell the server we are closing the h2 connection
    h2_connection.close_connection()
    tls_socket.sendall(h2_connection.data_to_send())

    # close the socket
    tls_socket.close()

    if error_code != 0:
        exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="Port to use")
    parser.add_argument("path", help="The path to request")
    parser.add_argument("-n", type=int, default=1, help="Number of streams to open")

    args = parser.parse_args()

    make_request(args.port, args.path, args.n)


if __name__ == '__main__':
    main()
