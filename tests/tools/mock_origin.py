#!/usr/bin/env python3
'''
A reusable mock origin server for ATS autests.

Replaces the various ad-hoc nc-based shell scripts (post/server1.sh,
chunked_encoding/server2..4.sh, post_slow_server/server.sh) with a single
Python tool that:

  - Handles When.PortOpen() readiness probes gracefully (nc -l cannot).
  - Accepts one real HTTP request, optionally saves it to a file, and sends
    a configurable response.
  - Drains remaining request data after responding so that ATS does not see
    a connection reset while still forwarding a POST body (avoids HTTP/2 502).
  - Supports Content-Length bodies, chunked transfer encoding, and arbitrary
    response delays.

Usage examples mapping to the original shell scripts:

  # post/server1.sh PORT OUTFILE
  mock_origin.py PORT --output OUTFILE --status 420 --reason "Be Calm"

  # chunked_encoding/server2.sh PORT OUTFILE  (Content-Length body)
  mock_origin.py PORT --output OUTFILE --body "123456789012345"

  # chunked_encoding/server3.sh PORT OUTFILE  (Chunked body)
  mock_origin.py PORT --output OUTFILE --body "123456789012345" --chunked

  # post_slow_server/server.sh PORT  (Delayed 200KB response)
  mock_origin.py PORT --output rcv_file --delay 120 --body-size 204800
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
import socket
import sys
import time

FILLER_LINE_WIDTH = 8


def build_response(args):
    '''Build the complete HTTP response bytes from CLI arguments.'''

    body = b''
    if args.body is not None:
        body = args.body.encode()
    elif args.body_size and args.body_size > 0:
        lines = []
        offset = 0
        while offset < args.body_size:
            offset += FILLER_LINE_WIDTH
            lines.append(f'{offset:07d}\n'.encode())
        body = b''.join(lines)[:args.body_size]

    status_line = f'HTTP/1.1 {args.status} {args.reason}\r\n'.encode()

    if args.chunked:
        headers = b'Transfer-Encoding: chunked\r\n'
        for h in (args.header or []):
            headers += h.encode() + b'\r\n'
        headers += b'\r\n'
        chunk = f'{len(body):X}\r\n'.encode() + body + b'\r\n'
        terminator = b'0\r\n\r\n'
        return status_line + headers + chunk + terminator
    else:
        headers = f'Content-Length: {len(body)}\r\n'.encode()
        for h in (args.header or []):
            headers += h.encode() + b'\r\n'
        headers += b'\r\n'
        return status_line + headers + body


def serve_one(args):
    '''Listen, absorb readiness probes, serve one real HTTP transaction, exit.'''

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', args.port))
    sock.listen(1)

    response = build_response(args)

    while True:
        conn, addr = sock.accept()
        data = b''
        try:
            while True:
                chunk = conn.recv(65536)
                if not chunk:
                    break
                data += chunk
                if b'\r\n\r\n' in data:
                    break
        except ConnectionError:
            pass

        if not data:
            # Readiness probe (e.g. When.PortOpen) -- connected and
            # disconnected without sending data.  Go back to waiting.
            conn.close()
            continue

        # Real HTTP request arrived.
        if args.output:
            with open(args.output, 'wb') as f:
                f.write(data)

        if args.delay > 0:
            time.sleep(args.delay)

        try:
            conn.sendall(response)
        except ConnectionError:
            pass

        # Drain remaining request data (e.g. a large POST body that is still
        # being forwarded by ATS).  Closing without draining causes a TCP RST
        # which makes ATS return 502 on HTTP/2 streams.
        try:
            while True:
                if not conn.recv(65536):
                    break
        except ConnectionError:
            pass

        conn.close()
        break

    sock.close()


def main():
    parser = argparse.ArgumentParser(
        description='Mock origin server for ATS autests. '
        'Listens on PORT, serves one HTTP transaction, then exits. '
        'Compatible with When.PortOpen() readiness probes.')

    parser.add_argument('port', type=int, help='TCP port to listen on')
    parser.add_argument('--output', '-o', help='Write received request data to FILE')
    parser.add_argument('--status', '-s', type=int, default=200, help='HTTP status code (default: 200)')
    parser.add_argument('--reason', '-r', default='OK', help='HTTP reason phrase (default: OK)')
    parser.add_argument('--header', action='append', help='Additional response header (repeatable), e.g. "X-Foo: bar"')
    parser.add_argument('--body', '-b', help='Response body string')
    parser.add_argument('--body-size', type=int, default=0, help='Generate N bytes of filler body data')
    parser.add_argument('--chunked', action='store_true', help='Use chunked transfer encoding')
    parser.add_argument('--delay', '-d', type=float, default=0, help='Seconds to delay before sending response')

    args = parser.parse_args()
    serve_one(args)


if __name__ == '__main__':
    main()
