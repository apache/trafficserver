#!/usr/bin/env python3
"""A client that sends an HTTP request and immediately aborts."""

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


def main() -> int:
    """Connect, send a partial request, and abort."""
    parser = argparse.ArgumentParser(description='Send a partial request and abort.')
    parser.add_argument('host', help='The host to connect to.')
    parser.add_argument('port', type=int, help='The port to connect to.')
    args = parser.parse_args()

    # Connect to the server.
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))
    print(f'Connected to {args.host}:{args.port}')

    # Send ONLY partial request headers (no terminating \r\n\r\n).
    # This means ATS will wait for more data and never construct a response.
    partial_request = b"GET / HTTP/1.1\r\nHost: www.example.com\r\n"
    sock.sendall(partial_request)
    print('Sent partial request (missing final CRLF), aborting...')

    # Immediately close the socket.
    # This triggers an ERR_CLIENT_ABORT before any response is constructed.
    sock.close()
    print('Connection closed.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
