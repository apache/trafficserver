#!/usr/bin/env python3
"""Origin server that sends compressible response headers but no body.

The response advertises a Content-Length but none of the body is sent, so a
response transform never receives any data before the body is cut short. The
request path selects how the body is cut short:

* /close/...: close the connection shortly after the headers.
* /stall/...: hold the connection open after the headers so that the ATS
  server no-activity timeout fires.
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
import threading
import time

ADVERTISED_LENGTH = 50000
# Give ATS time to process the headers before the close so that the
# truncation is detected while tunneling the body.
CLOSE_DELAY_SECONDS = 0.5
STALL_SECONDS = 60


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True, help='Port to listen on')
    return parser.parse_args()


def read_request(conn: socket.socket) -> bytes | None:
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return None
        data += chunk
    return data


def handle_connection(conn: socket.socket) -> None:
    try:
        request = read_request(conn)
        if request is None:
            return
        path = request.split(b'\r\n', 1)[0].split(b' ')[1]
        conn.sendall(
            b'HTTP/1.1 200 OK\r\n'
            b'Content-Type: application/javascript\r\n'
            b'Cache-Control: public, max-age=3600\r\n'
            b'Content-Length: ' + str(ADVERTISED_LENGTH).encode() + b'\r\n'
            b'\r\n')
        time.sleep(STALL_SECONDS if path.startswith(b'/stall/') else CLOSE_DELAY_SECONDS)
    except Exception as e:
        print(f'Error handling connection: {e}', file=sys.stderr)
    finally:
        conn.close()


def main() -> int:
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    args = parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('127.0.0.1', args.port))
    sock.listen(16)
    print(f'Listening on port {args.port}', flush=True)

    while True:
        conn, _ = sock.accept()
        threading.Thread(target=handle_connection, args=(conn,), daemon=True).start()


if __name__ == '__main__':
    sys.exit(main())
