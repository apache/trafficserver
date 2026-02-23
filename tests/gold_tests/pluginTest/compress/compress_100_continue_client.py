#!/usr/bin/env python3
"""Client that sends a POST with Expect: 100-continue through ATS.

Used to reproduce the crash in HttpTunnel::producer_run when compress.so
with cache=true is combined with a 100 Continue response from the origin.
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


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('proxy_host', help='ATS proxy host')
    parser.add_argument('proxy_port', type=int, help='ATS proxy port')
    return parser.parse_args()


def read_until_headers(sock):
    """Read until we get a complete set of HTTP headers."""
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = sock.recv(4096)
        if not chunk:
            return data
        data += chunk
    return data


def drain_response_body(sock, headers_data):
    """Read remaining body bytes based on Content-Length or until connection close."""
    header_end = headers_data.find(b'\r\n\r\n') + 4
    headers_str = headers_data[:header_end].decode('utf-8', errors='replace')
    body_after_headers = headers_data[header_end:]

    cl = 0
    for line in headers_str.split('\r\n'):
        if line.lower().startswith('content-length:'):
            cl = int(line.split(':', 1)[1].strip())
            break

    remaining = cl - len(body_after_headers)
    while remaining > 0:
        chunk = sock.recv(min(remaining, 4096))
        if not chunk:
            break
        remaining -= len(chunk)


def main():
    args = parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect((args.proxy_host, args.proxy_port))

    body = b'test body data'

    # Send POST request with Expect: 100-continue and Accept-Encoding: gzip.
    # POST is used because the origin must wait for the request body before
    # sending 200 OK.  This ensures the 100 Continue tunnel completes and
    # client_response_hdr_bytes is set BEFORE the 200 OK is processed,
    # which is needed to trigger the specific crash at HttpTunnel.cc:1006.
    # POST caching is enabled via proxy.config.http.cache.post_method: 1.
    request = (
        b'POST /test/resource.js HTTP/1.1\r\n'
        b'Host: example.com\r\n'
        b'Accept-Encoding: gzip\r\n'
        b'Expect: 100-continue\r\n'
        b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
        b'Connection: close\r\n'
        b'\r\n'
    )
    sock.sendall(request)
    print('Sent POST with Expect: 100-continue', flush=True)

    # Read the 100 Continue response
    response_100 = read_until_headers(sock)
    status_line = response_100.split(b'\r\n')[0].decode('utf-8', errors='replace')
    print(f'Received: {status_line}', flush=True)

    if b'100' not in response_100.split(b'\r\n')[0]:
        print(f'ERROR: Expected 100 Continue, got: {status_line}', file=sys.stderr)
        sock.close()
        return 1

    # Send the POST body after receiving 100 Continue.
    sock.sendall(body)
    print('Sent request body', flush=True)

    # Read the final 200 OK response (may already be partially buffered
    # after the 100 Continue headers).
    remaining_after_100 = response_100[response_100.find(b'\r\n\r\n') + 4:]
    if b'\r\n\r\n' in remaining_after_100:
        final_headers = remaining_after_100
    else:
        final_headers = remaining_after_100 + read_until_headers(sock)

    if final_headers:
        status_line = final_headers.split(b'\r\n')[0].decode('utf-8', errors='replace')
        print(f'Received final: {status_line}', flush=True)
        drain_response_body(sock, final_headers)
    else:
        print('No final response received', flush=True)

    sock.close()
    print('Done.', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
