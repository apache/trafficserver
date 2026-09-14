#!/usr/bin/env python3
"""Send a partial POST to trigger the abort_tunnel code path.

Sends POST headers claiming a large Content-Length but only sends a small
chunk of body data. When a request transform plugin is active, this causes
ATS to call abort_tunnel() while the transform entry is still in the vc_table.
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

import socket
import sys


def main() -> int:
    """Run the client."""
    host = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

    request = (
        'POST / HTTP/1.1\r\n'
        'Host: quick.server.com\r\n'
        'Content-Type: application/octet-stream\r\n'
        'Content-Length: 100000\r\n'
        '\r\n').encode()

    partial_body = b'x' * 4096

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((host, port))
        sock.sendall(request + partial_body)
        print(f'Sent POST headers (Content-Length: 100000) + {len(partial_body)} bytes')

        try:
            response = sock.recv(4096)
        except ConnectionError:
            print('Connection reset (expected for partial POST)')
            return 0
        except socket.timeout:
            print('ERROR: timeout waiting for response', file=sys.stderr)
            return 1

        if response:
            first_line = response.split(b'\r\n')[0].decode(errors='replace')
            print(first_line)
            if first_line.startswith('HTTP/1.1'):
                return 0
            print('ERROR: unexpected response', file=sys.stderr)
            return 1
        else:
            print('Connection closed (expected for partial POST)')
            return 0
    finally:
        sock.close()


if __name__ == '__main__':
    sys.exit(main())
