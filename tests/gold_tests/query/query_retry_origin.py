'''
An origin that closes the first connection after reading the request, without
responding, and answers every later request with a 200.
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


def read_request(conn: socket.socket) -> tuple[str, bytes]:
    data = bytearray()
    while b'\r\n\r\n' not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return '', b''
        data += chunk
    head, _, rest = data.partition(b'\r\n\r\n')
    body = bytearray(rest)
    lines = head.decode('latin-1').split('\r\n')
    method = lines[0].split(' ')[0]
    length = 0
    for line in lines[1:]:
        name, _, value = line.partition(':')
        if name.strip().lower() == 'content-length':
            length = int(value.strip())
    while len(body) < length:
        chunk = conn.recv(4096)
        if not chunk:
            break
        body += chunk
    return method, bytes(body[:length])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', args.port))
        listener.listen()
        attempt = 0
        while True:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(10)
                try:
                    method, body = read_request(conn)
                except OSError as e:
                    print(f'request read failed: {e}', flush=True)
                    continue
                if not method:
                    continue
                attempt += 1
                text = body.decode('utf-8', errors='replace')
                print(f'request attempt={attempt} method={method} body={text}', flush=True)
                if attempt == 1:
                    continue
                payload = b'retried'
                conn.sendall(
                    b'HTTP/1.1 200 OK\r\nContent-Length: ' + str(len(payload)).encode() + b'\r\nConnection: close\r\n\r\n' +
                    payload)


if __name__ == '__main__':
    sys.exit(main())
