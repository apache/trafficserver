#!/usr/bin/env python3
"""Send a request over a raw socket and print the responses received.

Two request shapes are supported: a body-less POST (Content-Length: 0) followed
by a second pipelined request on the same connection, and a single request that
carries conflicting Content-Length header fields.
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
import socket
import sys


def parse_args() -> argparse.Namespace:
    """Parse the command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("proxy_address", help="Address of the proxy to connect to.")
    parser.add_argument("proxy_port", type=int, help="The port of the proxy to connect to.")
    parser.add_argument("host", help="The Host header field value to use.")
    parser.add_argument(
        "mode",
        nargs='?',
        default='pipeline',
        choices=['pipeline', 'conflicting_cl'],
        help="Which request to send: a body-less POST followed by a pipelined "
        "request, or a request with conflicting Content-Length headers.")
    return parser.parse_args()


def build_request(mode: str, host: str) -> bytes:
    """Build the raw request bytes for the given mode.

    :param mode: 'pipeline' for a body-less POST followed by a pipelined GET, or
        'conflicting_cl' for a request carrying conflicting Content-Length
        header fields.
    :param host: The Host header field value.
    :returns: The raw request bytes.
    """
    if mode == 'conflicting_cl':
        # Two different Content-Length values are an ambiguous framing that a
        # careful proxy must reject (RFC 9112 section 6.3) rather than forward,
        # since a downstream server might frame the body differently.
        return (
            f'POST / HTTP/1.1\r\n'
            f'Host: {host}\r\n'
            f'Content-Length: 0\r\n'
            f'Content-Length: 38\r\n'
            f'Connection: keep-alive\r\n'
            f'\r\n'
            f'GET /second HTTP/1.1\r\n'
            f'Host: {host}\r\n'
            f'X-Marker: second-request\r\n'
            f'\r\n').encode()

    return (
        f'POST / HTTP/1.1\r\n'
        f'Host: {host}\r\n'
        f'Content-Length: 0\r\n'
        f'Connection: keep-alive\r\n'
        f'\r\n'
        f'GET /second HTTP/1.1\r\n'
        f'Host: {host}\r\n'
        f'X-Marker: second-request\r\n'
        f'\r\n').encode()


def main() -> int:
    """Send the request and print the received responses."""
    args = parse_args()

    request = build_request(args.mode, args.host)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((args.proxy_address, args.proxy_port))
        print(f'Connected to {args.proxy_address}:{args.proxy_port}')
        print(f'Sending request ({len(request)} bytes):')
        print(request)
        sock.sendall(request)

        # The pipeline mode expects two responses; the conflicting_cl mode
        # expects a single 400.
        expected_responses = 1 if args.mode == 'conflicting_cl' else 2
        sock.settimeout(5.0)
        responses = b""
        reached_expected = False
        try:
            while True:
                data = sock.recv(4096)
                if not data:
                    break
                responses += data
                # Each response terminates its header block with a blank line.
                # Key off that rather than a trailing newline in the payload,
                # since ATS-generated error bodies need not end in a newline.
                if not reached_expected and responses.count(b'\r\n\r\n') >= expected_responses:
                    # Got the expected responses. Shorten the timeout so an
                    # unexpected extra response is still caught without waiting
                    # out the full initial timeout.
                    reached_expected = True
                    sock.settimeout(1.0)
        except socket.timeout:
            if not reached_expected:
                print('Read timed out.')

    print('==== RESPONSES RECEIVED ====')
    print(responses.decode(errors='replace'))
    print('==== END RESPONSES ====')
    print(f'STATUS_LINE_COUNT: {responses.count(b"HTTP/1.1")}')
    return 0


if __name__ == "__main__":
    sys.exit(main())
