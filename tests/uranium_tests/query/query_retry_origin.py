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
from pathlib import Path
import socket
import ssl
import sys
import time


def read_request(conn: socket.socket) -> tuple[str, bytes]:
    """Read one request and its declared content.

    :param conn: Connected origin socket.
    """
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
    print(f'request headers method={method}', flush=True)
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
    """Close or corrupt the first origin response, then serve retries."""
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int)
    parser.add_argument('--tls', nargs=2, metavar=('CERTIFICATE', 'KEY'))
    parser.add_argument('--upload-log', type=Path)
    parser.add_argument('--handshake-error', action='store_true')
    args = parser.parse_args()
    context = None
    if args.tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(*args.tls)
        context.set_alpn_protocols(['http/1.1'])

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', args.port))
        listener.listen()
        attempt = 0
        handshake_failed = False
        while True:
            conn, _ = listener.accept()
            conn.settimeout(10)
            if context:
                if args.handshake_error and not handshake_failed:
                    with conn:
                        if not conn.recv(1, socket.MSG_PEEK):
                            continue
                        conn.sendall(b'not a TLS record\r\n')
                    handshake_failed = True
                    print('TLS handshake failed before request', flush=True)
                    continue
                try:
                    conn = context.wrap_socket(conn, server_side=True)
                except ssl.SSLError:
                    conn.close()
                    continue
            with conn:
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
                if attempt == 1 and not args.handshake_error:
                    if context:
                        if args.upload_log and body:
                            deadline = time.monotonic() + 5
                            while '[tunnel_handler_post_server, VC_EVENT_WRITE_COMPLETE' not in args.upload_log.read_text():
                                if time.monotonic() >= deadline:
                                    raise RuntimeError('ATS did not finish the request body tunnel')
                                time.sleep(0.01)
                        # A plaintext TLS record triggers a read error instead of a clean TLS close.
                        with socket.socket(fileno=conn.detach()) as raw:
                            raw.sendall(b'not a TLS record\r\n')
                    continue
                payload = b'retried'
                conn.sendall(
                    b'HTTP/1.1 200 OK\r\nContent-Length: ' + str(len(payload)).encode() + b'\r\nConnection: close\r\n\r\n' + payload
                )


if __name__ == '__main__':
    sys.exit(main())
