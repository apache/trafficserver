'''
Fetch stats_over_http output and verify the test_metrics.so gauges in the decoded body.
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
import gzip
import http.client
import json
import socket
import struct
import subprocess
import sys
import zlib


def decode(encoding: str | None, data: bytes) -> bytes:
    if encoding is None:
        return data
    if encoding == 'gzip':
        return gzip.decompress(data)
    if encoding == 'deflate':
        return zlib.decompress(data)
    return subprocess.run(['brotli', '--decompress', '--stdout'], input=data, capture_output=True, check=True).stdout


def test_metric_values(stats_format: str, body: str) -> list[int]:
    '''Return the sorted values of the test_metrics.so gauges in a stats body.'''
    if stats_format == 'json':
        items = json.loads(body)['global'].items()
        prefix = 'plugin.test_metrics.gauge_'
    elif stats_format == 'csv':
        items = (line.split(',', 1) for line in body.splitlines())
        prefix = 'plugin.test_metrics.gauge_'
    else:
        # The Prometheus v2 format can turn part of a name into a label, so match the prefix only.
        items = (line.rsplit(' ', 1) for line in body.splitlines() if line and not line.startswith('#'))
        prefix = 'plugin_test_metrics_gauge'
    return sorted(int(value) for name, value in items if name.startswith(prefix))


def get(args: argparse.Namespace, headers: dict[str, str]) -> tuple[http.client.HTTPResponse, bytes]:
    headers = {'Connection': 'close', **headers}
    if args.encoding:
        headers['Accept-Encoding'] = args.encoding
    conn = http.client.HTTPConnection('127.0.0.1', args.port, timeout=60)
    conn.request('GET', f'/_stats/{args.format}', headers=headers)
    response = conn.getresponse()
    body = response.read()
    conn.close()
    return response, body


def fetch_and_verify(args: argparse.Namespace) -> int:
    response, encoded = get(args, {})
    content_encoding = response.getheader('Content-Encoding')
    if response.status != 200 or content_encoding != args.encoding:
        print(f'Unexpected response: status {response.status}, Content-Encoding {content_encoding}')
        return 1

    body = decode(args.encoding, encoded).decode('utf-8')
    values = test_metric_values(args.format, body)
    print(f'{args.format} {args.encoding}: {len(encoded)} encoded bytes, {len(body)} decoded bytes')
    if values != list(range(args.count)):
        print(f'Found {len(values)} test metrics, expected {args.count} with values 0 to {args.count - 1}')
        return 1
    print(f'Verified {len(values)} test metrics')
    return 0


def reset_after_first_byte(args: argparse.Namespace) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # A small receive window keeps most of the body in Traffic Server when the client goes away.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    sock.settimeout(60)
    sock.connect(('127.0.0.1', args.port))
    request = f'GET /_stats/{args.format} HTTP/1.1\r\nHost: 127.0.0.1\r\n'
    if args.encoding:
        request += f'Accept-Encoding: {args.encoding}\r\n'
    sock.sendall(f'{request}\r\n'.encode())
    first = sock.recv(1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    sock.close()
    print(f'Reset the connection after {len(first)} response byte')
    return 0 if first else 1


def request_rejected(args: argparse.Namespace) -> int:
    # Traffic Server rejects "TE: identity;q=0" after stats_over_http has set up its intercept, so the
    # intercept is never connected.
    response, _ = get(args, {'TE': 'identity;q=0'})
    print(f'Response status {response.status}')
    return 0 if response.status == 406 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='Traffic Server HTTP port')
    parser.add_argument('format', choices=['json', 'csv', 'prometheus', 'prometheus_v2'])
    parser.add_argument('--encoding', choices=['gzip', 'deflate', 'br'], help='the content coding to request')
    parser.add_argument('--count', type=int, default=0, help='the --count given to test_metrics.so')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        '--reset', action='store_true', help='reset the connection after the first response byte instead of verifying the body')
    mode.add_argument('--reject', action='store_true', help='send a request that Traffic Server rejects with a 406')
    args = parser.parse_args()
    if args.reset:
        return reset_after_first_byte(args)
    if args.reject:
        return request_rejected(args)
    return fetch_and_verify(args)


if __name__ == '__main__':
    sys.exit(main())
