#!/usr/bin/env python3
'''
Start a TLS handshake with ATS and verify that ATS rejects the connection
instead of leaving it open.
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
import ssl
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='The ATS TLS port.')
    parser.add_argument('--timeout', type=float, default=5.0, help='Seconds to wait for ATS to reject the connection.')
    args = parser.parse_args()

    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    with socket.create_connection(('127.0.0.1', args.port), timeout=args.timeout) as sock:
        try:
            with context.wrap_socket(sock, server_hostname='example.com'):
                print('ATS completed the TLS handshake on a rejected connection')
                return 1
        except socket.timeout:
            print(f'ATS left the rejected connection open for {args.timeout} seconds')
            return 1
        except (ssl.SSLError, ConnectionError) as e:
            # A closed connection or a TLS alert both count as a rejection.
            print(f'ATS rejected the connection: {type(e).__name__}')
            return 0


if __name__ == '__main__':
    sys.exit(main())
