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


def makerequest(port: int, path: str, delay: int) -> None:
    """Establish an HTTP/2 connection and send a request.

    :param port: The port to connect to.
    :param path: The path to request.
    :param delay: The delay to wait between sending requests in a stream.
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

    h2_connection.send_headers(1, headers, end_stream=True)
    tls_socket.sendall(h2_connection.data_to_send())

    # delay, triggering ATS timeout.
    time.sleep(delay)

    # The following should fail due to the timeout.
    try:
        # Send a second request.
        h2_connection.send_headers(3, headers, end_stream=True)
        tls_socket.sendall(h2_connection.data_to_send())

        response_stream_ended = False
        body = b''
        while not response_stream_ended:
            # read raw data from the socket
            data = tls_socket.recv(65536 * 1024)
            if not data:
                break

            # feed raw data into h2, and process resulting events
            events = h2_connection.receive_data(data)
            for event in events:
                if isinstance(event, h2.events.ResponseReceived):
                    # response headers received
                    print("Response received:")
                    for header in event.headers:
                        print(f'  {header[0].decode()}: {header[1].decode()}')
                if isinstance(event, h2.events.DataReceived):
                    # update flow control so the server doesn't starve us
                    h2_connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    # more response body data received
                    body += event.data
                if isinstance(event, h2.events.StreamEnded):
                    # response body completed, let's exit the loop
                    response_stream_ended = True
                    break
            # send any pending data to the server
            tls_socket.sendall(h2_connection.data_to_send())

        print(f"Response fully received: {len(body)} bytes")

        body_str = body.decode('utf-8')

        # tell the server we are closing the h2 connection
        h2_connection.close_connection()
        tls_socket.sendall(h2_connection.data_to_send())

        # close the socket
        tls_socket.close()
    except Exception:
        print("CONNECTION_TIMEOUT")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="Port to use")
    parser.add_argument("path", help="The path to request")
    parser.add_argument("delay", type=int, help="The number of seconds to delay betwen requests in a stream")
    args = parser.parse_args()

    makerequest(args.port, args.path, args.delay)


if __name__ == '__main__':
    main()
