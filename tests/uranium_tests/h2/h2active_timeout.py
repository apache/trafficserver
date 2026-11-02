#!/usr/bin/env python3
'''
An h2 client built to trigger active timeout.
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
import time


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


def makerequest(port: int, path: str, active_timeout: int, max_wait: int) -> None:
    """Send a request, then wait for ATS to close the connection on its active timeout.

    ATS enforces the active timeout from its InactivityCop, which looks at a
    connection only once a second, so the close lands up to a second after the
    timeout expires, and later still on a loaded machine. Rather than guess when
    that is, wait for the close itself and check that it came no sooner than the
    timeout and within max_wait seconds.

    :param port: The port to connect to.
    :param path: The path to request.
    :param active_timeout: The proxy.config.http2.active_timeout_in ATS uses, in seconds.
    :param max_wait: The number of seconds to wait for ATS to close the connection.
    """

    tls_socket = get_socket(port)

    # ATS starts the active timeout when it reads the connection preface, which
    # happens after this point, so it cannot close the connection any sooner
    # than active_timeout seconds from now.
    start = time.monotonic()

    h2_connection = h2.connection.H2Connection()
    h2_connection.initiate_connection()
    tls_socket.sendall(h2_connection.data_to_send())

    headers = [
        (':method', 'GET'),
        (':path', path),
        (':authority', 'localhost'),
        (':scheme', 'https'),
    ]

    h2_connection.send_headers(1, headers, end_stream=True)
    tls_socket.sendall(h2_connection.data_to_send())

    # Read the response, then keep reading until ATS closes the connection.
    closed = False
    try:
        while True:
            remaining = start + max_wait - time.monotonic()
            if remaining <= 0:
                break
            tls_socket.settimeout(remaining)
            data = tls_socket.recv(65536)
            if not data:
                closed = True
                break
            for event in h2_connection.receive_data(data):
                if isinstance(event, h2.events.DataReceived):
                    h2_connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
            tls_socket.sendall(h2_connection.data_to_send())
    except socket.timeout:
        pass
    except (ConnectionError, ssl.SSLError):
        closed = True
    elapsed = time.monotonic() - start
    tls_socket.close()

    if not closed:
        print(f"Connection still open after {max_wait} seconds")
    elif elapsed < active_timeout:
        print(f"Connection closed after {elapsed:.1f} seconds, before the {active_timeout} second active timeout")
    else:
        print("CONNECTION_TIMEOUT")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="Port to use")
    parser.add_argument("path", help="The path to request")
    parser.add_argument("active_timeout", type=int, help="The active timeout ATS is configured with, in seconds")
    parser.add_argument("--max_wait", type=int, default=15, help="The number of seconds to wait for ATS to close the connection")
    args = parser.parse_args()

    makerequest(args.port, args.path, args.active_timeout, args.max_wait)


if __name__ == '__main__':
    main()
