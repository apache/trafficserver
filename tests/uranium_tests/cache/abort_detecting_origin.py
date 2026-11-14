#!/usr/bin/env python3
'''An origin server that reports whether the proxy closed the connection.

The server accepts a single connection, reads the request, then waits for the
configured delay before responding. While waiting, it watches the connection for
the proxy closing it, which is what a proxy is expected to do when its client
aborts the request before the origin responds.
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

import argparse
import select
import socket
import sys
import time

ABORT_DETECTED = 'proxy_closed_connection'
ABORT_NOT_DETECTED = 'proxy_kept_connection_open'

RESPONSE_BODY = b'0123456789'
RESPONSE = (
    b'HTTP/1.1 200 OK\r\n'
    b'Content-Type: text/plain\r\n'
    b'Cache-Control: max-age=300\r\n'
    b'Content-Length: ' + str(len(RESPONSE_BODY)).encode() + b'\r\n'
    b'Connection: close\r\n'
    b'\r\n' + RESPONSE_BODY
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='The port to listen on.')
    parser.add_argument('--delay', type=float, default=10.0, help='Seconds to wait before sending the response.')
    return parser.parse_args()


def read_request(connection: socket.socket) -> bool:
    '''Read the request headers off of the connection.

    :param connection: The accepted connection to read from.
    :returns: Whether a complete set of request headers was read.
    '''
    request = b''
    while b'\r\n\r\n' not in request:
        chunk = connection.recv(4096)
        if not chunk:
            return False
        request += chunk
    request_line = request.split(b'\r\n')[0].decode(errors='replace')
    print(f'Received request: {request_line}', flush=True)
    return True


def wait_for_abort(connection: socket.socket, delay: float) -> bool:
    '''Wait for the delay, watching for the peer closing the connection.

    :param connection: The accepted connection to watch.
    :param delay: The number of seconds to wait before giving up.
    :returns: Whether the peer closed the connection during the delay.
    '''
    deadline = time.monotonic() + delay
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        readable, _, _ = select.select([connection], [], [], remaining)
        if not readable:
            return False
        try:
            if not connection.recv(4096):
                return True
        except ConnectionResetError:
            return True


def main() -> int:
    args = parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', args.port))
        listener.listen(5)
        print(f'Listening on port {args.port}', flush=True)

        # Readiness probes connect and close without sending a request, so keep
        # accepting connections until a request arrives.
        while True:
            connection, _ = listener.accept()
            with connection:
                if not read_request(connection):
                    print('Connection closed before the request was complete.', flush=True)
                    continue
                if wait_for_abort(connection, args.delay):
                    print(ABORT_DETECTED, flush=True)
                    return 0
                print(ABORT_NOT_DETECTED, flush=True)
                connection.sendall(RESPONSE)
                return 0


if __name__ == '__main__':
    sys.exit(main())
