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
    parser.add_argument('--timeout', type=float, default=30.0, help='Seconds to wait for ATS to close the connection.')
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    with socket.create_connection(('127.0.0.1', args.port), timeout=args.timeout) as sock:
        with context.wrap_socket(sock, server_hostname=args.sni) as tls_sock:
            print(f'Completed the TLS handshake: {tls_sock.version()}')
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
