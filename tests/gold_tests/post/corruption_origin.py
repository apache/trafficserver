#!/usr/bin/env python3
"""Origin that sends a 301 without consuming the request body, then checks
whether a reused connection carries leftover (corrupted) data. Handles
multiple connections so that a fixed ATS can open a fresh one for the
second request."""

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
import threading
import time

VALID_METHODS = {'GET', 'POST', 'PUT', 'DELETE', 'HEAD', 'OPTIONS', 'PATCH'}


def read_until_headers_complete(conn: socket.socket) -> bytes:
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return data
        data += chunk
    return data


def is_valid_http_request_line(line: str) -> bool:
    parts = line.strip().split(' ')
    if len(parts) < 3:
        return False
    return parts[0] in VALID_METHODS and parts[-1].startswith('HTTP/')


def send_200(conn: socket.socket) -> None:
    ok_body = b'OK'
    conn.sendall(b'HTTP/1.1 200 OK\r\n'
                 b'Content-Length: ' + str(len(ok_body)).encode() + b'\r\n'
                 b'\r\n' + ok_body)


def handle_connection(conn: socket.socket, args: argparse.Namespace, result: dict) -> None:
    try:
        data = read_until_headers_complete(conn)
        if not data:
            # Readiness probe.
            conn.close()
            return

        first_line = data.split(b'\r\n')[0].decode('utf-8', errors='replace')

        if first_line.startswith('POST'):
            # First request: send 301 without consuming the body.
            time.sleep(args.delay)

            body = b'Redirecting'
            response = (
                b'HTTP/1.1 301 Moved Permanently\r\n'
                b'Location: http://example.com/\r\n'
                b'Connection: keep-alive\r\n'
                b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
                b'\r\n' + body)
            conn.sendall(response)

            # Wait for potential reuse on this connection.
            conn.settimeout(args.timeout)
            try:
                second_data = b''
                while b'\r\n' not in second_data:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    second_data += chunk

                if second_data:
                    second_line = second_data.split(b'\r\n')[0].decode('utf-8', errors='replace')
                    if is_valid_http_request_line(second_line):
                        send_200(conn)
                    else:
                        result['corrupted'] = True
                        err_body = b'corrupted'
                        conn.sendall(
                            b'HTTP/1.1 400 Bad Request\r\n'
                            b'Content-Length: ' + str(len(err_body)).encode() + b'\r\n'
                            b'\r\n' + err_body)
            except socket.timeout:
                pass

        elif first_line.startswith('GET'):
            # Second request on a new connection (fix is working).
            result['new_connection'] = True
            send_200(conn)

        conn.close()
    except Exception:
        try:
            conn.close()
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int)
    parser.add_argument('--delay', type=float, default=1.0)
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', args.port))
    sock.listen(5)
    sock.settimeout(args.timeout + 5)

    result = {'corrupted': False, 'new_connection': False}
    threads = []
    connections_handled = 0

    try:
        while connections_handled < 10:
            try:
                conn, _ = sock.accept()
                t = threading.Thread(target=handle_connection, args=(conn, args, result))
                t.daemon = True
                t.start()
                threads.append(t)
                connections_handled += 1
            except socket.timeout:
                break
    except Exception:
        pass

    for t in threads:
        t.join(timeout=args.timeout + 2)

    sock.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
