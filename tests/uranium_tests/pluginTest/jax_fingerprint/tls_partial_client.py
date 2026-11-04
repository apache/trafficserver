#!/usr/bin/env python3
'''Complete a TLS handshake with ATS, optionally send a partial request, and wait for ATS to close.'''
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
import ssl
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='The ATS TLS port.')
    parser.add_argument('sni', help='The SNI to send in the ClientHello.')
    parser.add_argument('--path', help='Send an incomplete request header for this path. Without it, send nothing.')
    parser.add_argument(
        '--complete-first',
        metavar='UUID',
        help='Before the incomplete request, send a complete keep-alive request with this uuid and read its response.')
    parser.add_argument('--timeout', type=float, default=30.0, help='Seconds to wait for ATS to close the connection.')
    return parser.parse_args()


def read_response(tls_sock: ssl.SSLSocket) -> bytes:
    """Read one response whose body length is given by Content-Length.

    :param tls_sock: The connection to read from.
    :returns: The response header block and body.
    """
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = tls_sock.recv(4096)
        if not chunk:
            raise ConnectionError('ATS closed the connection before sending a complete response.')
        data += chunk
    header, _, body = data.partition(b'\r\n\r\n')
    content_length = 0
    for line in header.split(b'\r\n')[1:]:
        name, _, value = line.partition(b':')
        if name.strip().lower() == b'content-length':
            content_length = int(value.strip())
    while len(body) < content_length:
        chunk = tls_sock.recv(4096)
        if not chunk:
            raise ConnectionError('ATS closed the connection before sending the response body.')
        body += chunk
    return header + b'\r\n\r\n' + body


def main() -> int:
    args = parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    with socket.create_connection(('127.0.0.1', args.port), timeout=args.timeout) as sock:
        with context.wrap_socket(sock, server_hostname=args.sni) as tls_sock:
            print(f'Completed the TLS handshake: {tls_sock.version()}')
            if args.complete_first:
                tls_sock.sendall(
                    f'GET /{args.complete_first} HTTP/1.1\r\nHost: {args.sni}\r\nuuid: {args.complete_first}\r\n\r\n'.encode())
                status_line = read_response(tls_sock).split(b'\r\n', 1)[0].decode()
                print(f'Received a response to the complete request: {status_line}')
            if args.path:
                # The header block is never terminated, so ATS waits for the rest of it until it times out.
                tls_sock.sendall(f'GET {args.path} HTTP/1.1\r\nHost: {args.sni}\r\n'.encode())
                print(f'Sent an incomplete request header for {args.path}')
            try:
                while tls_sock.recv(4096):
                    pass
            except (ssl.SSLError, ConnectionResetError) as e:
                print(f'Connection ended with: {e}')
            except socket.timeout:
                print('ATS did not close the connection.')
                return 1
    print('ATS closed the connection.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
