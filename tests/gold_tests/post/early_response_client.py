#!/usr/bin/env python3
'''Send a POST whose body is never completed, then a following request.

Raw sockets rather than curl so the request body can be left unfinished and so
no proxy environment variables are consulted.
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
import sys
import time

DECLARED_BODY_LENGTH = 100000


def read_response(conn: socket.socket) -> bytes:
    '''Read one response head.

    :param conn: Connection to read from.
    :return: The bytes read, empty on timeout or close.
    '''
    buf = b''
    while b'\r\n\r\n' not in buf:
        try:
            chunk = conn.recv(4096)
        except (OSError, socket.timeout):
            break
        if not chunk:
            break
        buf += chunk
    return buf


def status_of(response: bytes) -> str:
    '''Extract the status line of a response.

    :param response: The response bytes.
    :return: The status line, or NO-RESPONSE when nothing arrived.
    '''
    if not response:
        return 'NO-RESPONSE'
    return response.split(b'\r\n', 1)[0].decode(errors='replace')


def main() -> int:
    '''Drive the two requests.

    :return: Process exit status.
    '''
    port = int(sys.argv[1])

    # Declare a body and send none of it. The origin answers early, so ATS is left
    # holding a request body it has not finished forwarding.
    first = socket.create_connection(('127.0.0.1', port), timeout=10)
    first.sendall(
        b'POST /early HTTP/1.1\r\n'
        b'Host: example.com\r\n'
        b'Content-Length: ' + str(DECLARED_BODY_LENGTH).encode() + b'\r\n'
        b'\r\n')
    print(f'EARLY-POST STATUS {status_of(read_response(first))}', flush=True)
    first.close()

    # Give ATS time to finish the transaction and decide what to do with the
    # origin connection.
    time.sleep(2)

    # A separate, ordinary request. It must reach the origin as a request.
    second = socket.create_connection(('127.0.0.1', port), timeout=10)
    second.sendall(b'GET /probe HTTP/1.1\r\n'
                   b'Host: example.com\r\n'
                   b'\r\n')
    response = read_response(second)
    print(f'PROBE STATUS {status_of(response)}', flush=True)
    second.close()

    return 0


if __name__ == '__main__':
    sys.exit(main())
