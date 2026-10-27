#!/usr/bin/env python3
'''Leave a request body unfinished, then send a following request.

Raw sockets rather than curl so a declared body can be left unsent, and so no
proxy environment variables are consulted.

Two scenarios, which differ in how the origin answers early and therefore in
which ATS release path decides what happens to the origin connection:

  abandoned-tunnel  POST answered 420 early, which builds a response tunnel.
  revalidate-304    body-carrying GET on a stale entry answered 304 early, which
                    builds no response tunnel.
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


def request(port: int, head: bytes, label: str) -> bytes:
    '''Send one request on its own connection and report the status.

    :param port: ATS port to connect to.
    :param head: The complete request head to send.
    :param label: Label used in the reported line.
    :return: The response bytes.
    '''
    conn = socket.create_connection(('127.0.0.1', port), timeout=10)
    try:
        conn.sendall(head)
        response = read_response(conn)
        print(f'{label} STATUS {status_of(response)}', flush=True)
        return response
    finally:
        conn.close()


def body_carrying(method: str, path: str) -> bytes:
    '''Build a request head that declares a body which will never be sent.

    :param method: Request method.
    :param path: Request target.
    :return: The request head bytes.
    '''
    return (f'{method} {path} HTTP/1.1\r\n'
            f'Host: example.com\r\n'
            f'Content-Length: {DECLARED_BODY_LENGTH}\r\n'
            f'\r\n').encode()


def main() -> int:
    '''Drive the requested scenario.

    :return: Process exit status.
    '''
    port = int(sys.argv[1])
    scenario = sys.argv[2]

    if scenario == 'abandoned-tunnel':
        # The origin answers before ATS has finished forwarding the body, so ATS
        # is left holding a body it has not finished sending.
        request(port, body_carrying('POST', '/early'), 'EARLY-POST')
    elif scenario == 'revalidate-304':
        # Prime the cache with an entry that is immediately stale.
        request(port, b'GET /revalidate HTTP/1.1\r\nHost: example.com\r\n\r\n', 'PRIME')
        time.sleep(1)
        # Revalidating request that also declares a body. The origin answers 304
        # without reading it, and ATS serves the cached copy.
        request(port, body_carrying('GET', '/revalidate'), 'REVALIDATE')
    else:
        print(f'unknown scenario {scenario}', file=sys.stderr)
        return 1

    # Give ATS time to finish the transaction and decide what to do with the
    # origin connection.
    time.sleep(2)

    # A separate, ordinary request. It must reach the origin as a request.
    request(port, b'GET /probe HTTP/1.1\r\nHost: example.com\r\n\r\n', 'PROBE')

    return 0


if __name__ == '__main__':
    sys.exit(main())
