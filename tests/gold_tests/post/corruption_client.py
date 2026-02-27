#!/usr/bin/env python3
"""Client that sends two requests on one TCP connection to reproduce
100-continue connection pool corruption."""

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

from http_utils import wait_for_headers_complete, determine_outstanding_bytes_to_read, drain_socket

import argparse
import socket
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('proxy_address')
    parser.add_argument('proxy_port', type=int)
    parser.add_argument('-s', '--server-hostname', dest='server_hostname', default='example.com')
    args = parser.parse_args()

    host = args.server_hostname
    body_size = 103
    body_data = b'X' * body_size

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.proxy_address, args.proxy_port))

    with sock:
        # Request 1: POST with Expect: 100-continue and a body.
        request1 = (
            f'POST /expect-100-corrupted HTTP/1.1\r\n'
            f'Host: {host}\r\n'
            f'Connection: keep-alive\r\n'
            f'Content-Length: {body_size}\r\n'
            f'Expect: 100-continue\r\n'
            f'\r\n').encode()
        sock.sendall(request1)

        # Send the body after a short delay without waiting for 100-continue.
        time.sleep(0.5)
        sock.sendall(body_data)

        # Drain the response (might be 100 + 301, or just 301).
        resp1_data = wait_for_headers_complete(sock)

        # If we got a 100 Continue, read past it to the real response.
        if b'100' in resp1_data.split(b'\r\n')[0]:
            after_100 = resp1_data.split(b'\r\n\r\n', 1)[1] if b'\r\n\r\n' in resp1_data else b''
            if b'\r\n\r\n' not in after_100:
                after_100 += wait_for_headers_complete(sock)
            resp1_data = after_100

        # Drain the response body.
        try:
            outstanding = determine_outstanding_bytes_to_read(resp1_data)
            if outstanding > 0:
                drain_socket(sock, resp1_data, outstanding)
        except ValueError:
            pass

        # Let ATS pool the origin connection.
        time.sleep(0.5)

        # Request 2: plain GET on the same client connection.
        request2 = (f'GET /second-request HTTP/1.1\r\n'
                    f'Host: {host}\r\n'
                    f'Connection: close\r\n'
                    f'\r\n').encode()
        sock.sendall(request2)

        resp2_data = wait_for_headers_complete(sock)
        status_line = resp2_data.split(b'\r\n')[0]

        if b'400' in status_line or b'corrupted' in resp2_data.lower():
            print('Corruption detected: second request saw corrupted data', flush=True)
        elif b'502' in status_line:
            print('Corruption detected: ATS returned 502 (origin parse error)', flush=True)
        else:
            print('No corruption: second request completed normally', flush=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
