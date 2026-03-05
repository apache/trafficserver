#!/usr/bin/env python3
"""Origin server that sends 100 Continue then a compressible 200 OK.

Used to reproduce the crash in HttpTunnel::producer_run when compress.so
with cache=true is combined with a 100 Continue response from the origin.
"""

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
import signal
import socket
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True, help='Port to listen on')
    return parser.parse_args()


def read_request(conn):
    """Read an HTTP request from the connection, returning the raw bytes."""
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return None
        data += chunk
    return data


def handle_connection(conn, addr):
    """Handle a single client connection."""
    try:
        conn.settimeout(10)
        request = read_request(conn)
        if request is None:
            return

        # Send 100 Continue immediately.
        conn.sendall(b'HTTP/1.1 100 Continue\r\n\r\n')

        # Read the request body.  The origin MUST wait for the body
        # before sending the 200 OK so that ATS processes the 100
        # Continue tunnel, the POST body tunnel, and THEN reads the
        # 200 OK - reproducing the exact timing of the production crash.
        request_str = request.decode('utf-8', errors='replace')
        cl = 0
        for line in request_str.split('\r\n'):
            if line.lower().startswith('content-length:'):
                cl = int(line.split(':', 1)[1].strip())
                break

        header_end = request.find(b'\r\n\r\n') + 4
        body_received = len(request) - header_end
        remaining = cl - body_received
        while remaining > 0:
            chunk = conn.recv(min(remaining, 4096))
            if not chunk:
                break
            remaining -= len(chunk)

        # Send a compressible 200 OK response with Content-Length (non-chunked).
        # The body is JavaScript text so compress.so will add a transform.
        # The body must be SMALLER than the 100 Continue response headers
        # (~82 bytes) so that skip_bytes > read_avail() in producer_run.
        body = b'var x=1;'  # 8 bytes - smaller than 100 Continue headers
        response = (
            b'HTTP/1.1 200 OK\r\n'
            b'Content-Type: text/javascript\r\n'
            b'Cache-Control: public, max-age=3600\r\n'
            b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
            b'\r\n' + body)
        conn.sendall(response)
    except Exception as e:
        print(f'Error handling connection from {addr}: {e}', file=sys.stderr)
    finally:
        conn.close()


def main():
    # Exit cleanly when the test framework sends SIGINT to stop us.
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))

    args = parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('127.0.0.1', args.port))
    sock.listen(5)
    sock.settimeout(5)

    actual_port = sock.getsockname()[1]
    print(f'Listening on port {actual_port}', flush=True)

    # Accept connections until timeout.  The first connection may be
    # the readiness probe from the test framework; the real ATS
    # connection arrives after that.
    while True:
        try:
            conn, addr = sock.accept()
            handle_connection(conn, addr)
        except socket.timeout:
            break

    sock.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
