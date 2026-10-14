'''
Client actions for connections_throttle_exempt.test.py.
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
import signal
import socket
import ssl
import subprocess
import sys
import time

TIMEOUT = 10.0
POLL_INTERVAL = 0.1


def proxy_header(source: str, port: int) -> bytes:
    """Return a PROXY protocol v1 header that claims the connection comes from @a source."""
    if ':' in source:
        return f'PROXY TCP6 {source} ::1 40000 {port}\r\n'.encode()
    return f'PROXY TCP4 {source} 127.0.0.1 40000 {port}\r\n'.encode()


def connect(host: str, port: int, tls: bool, proxy_source: str | None = None) -> socket.socket:
    """Open a connection to @a host, an IPv4 or IPv6 address.

    :param proxy_source: Start with a PROXY protocol header that claims this source address.
    """
    sock = socket.create_connection((host, port), timeout=TIMEOUT)
    if proxy_source:
        sock.sendall(proxy_header(proxy_source, port))
    if tls:
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        sock = context.wrap_socket(sock, server_hostname='healthcheck')
    return sock


def metric(name: str) -> str:
    """Return the traffic_ctl output line for metric @a name."""
    result = subprocess.run(['traffic_ctl', 'metric', 'get', name], capture_output=True, text=True, check=True)
    return result.stdout.strip()


def exchange(sock: socket.socket, close: bool) -> str:
    """Send a health check request on @a sock and return the response status line.

    :param close: Ask the proxy to close the connection after the response.
    :return: The status line, or an empty string if the proxy closed the connection first.
    """
    request = b'GET / HTTP/1.1\r\nHost: healthcheck\r\n'
    if close:
        request += b'Connection: close\r\n'
    try:
        sock.sendall(request + b'\r\n')
        response = b''
        while b'\r\n\r\n' not in response:
            data = sock.recv(4096)
            if not data:
                return ''
            response += data
    except (ConnectionResetError, BrokenPipeError, ssl.SSLError):
        return ''

    header, _, body = response.partition(b'\r\n\r\n')
    lines = header.decode().split('\r\n')
    length = 0
    for line in lines[1:]:
        name, _, value = line.partition(':')
        if name.strip().lower() == 'content-length':
            length = int(value)
    while len(body) < length:
        data = sock.recv(4096)
        if not data:
            break
        body += data
    return lines[0]


def wait_for_close(sock: socket.socket) -> None:
    """Return once the proxy has closed the TCP connection under @a sock."""
    if isinstance(sock, ssl.SSLSocket):
        # The TLS close_notify comes before the socket is closed, so read on to the TCP FIN.
        try:
            sock = sock.unwrap()
        except (OSError, ssl.SSLError):
            return
    try:
        while sock.recv(4096):
            pass
    except ConnectionResetError:
        pass


def attempt(args: argparse.Namespace) -> str:
    """Make one health check request and return whether it was served or refused."""
    try:
        sock = connect(args.host, args.port, args.tls, args.proxy_protocol)
    except (ConnectionResetError, BrokenPipeError, ssl.SSLError):
        # A TLS handshake or the PROXY header fails if the proxy closes the connection at accept.
        return 'refused'
    with sock:
        status = exchange(sock, close=not args.metric)
        if not status:
            return 'refused'
        print(f'served: {status}')
        if args.metric:
            for name in args.metric:
                print(f'while open: {metric(name)}')
            exchange(sock, close=True)
            wait_for_close(sock)
        return 'served'


def request(args: argparse.Namespace) -> int:
    """Make health check requests until the outcome is the one expected, or until --retry seconds pass."""
    deadline = time.monotonic() + args.retry
    outcome = attempt(args)
    while outcome != args.expect and time.monotonic() < deadline:
        time.sleep(POLL_INTERVAL)
        outcome = attempt(args)
    print(outcome)
    return 0 if outcome == args.expect else 1


def hold(args: argparse.Namespace) -> int:
    """Open connections that send part of a request, and hold them until terminated."""
    socks = [connect(args.host, args.port, tls=False) for _ in range(args.count)]
    for sock in socks:
        # The header is incomplete, so the proxy keeps the connection open and waits for the rest.
        sock.sendall(b'GET / HTTP/1.1\r\nHost: held\r\n')
    print(f'holding {len(socks)} connections', flush=True)
    try:
        signal.pause()
    except KeyboardInterrupt:
        pass
    return 0


def wait_metric(args: argparse.Namespace) -> int:
    """Wait until each metric NAME has its VALUE."""
    if len(args.metrics) % 2 != 0:
        print('expected NAME VALUE pairs')
        return 1
    expected = {f'{name} {value}' for name, value in zip(args.metrics[::2], args.metrics[1::2])}
    names = args.metrics[::2]
    deadline = time.monotonic() + TIMEOUT
    lines = {metric(name) for name in names}
    while lines != expected and time.monotonic() < deadline:
        time.sleep(POLL_INTERVAL)
        lines = {metric(name) for name in names}
    for line in sorted(lines):
        print(line)
    return 0 if lines == expected else 1


def wait_log(args: argparse.Namespace) -> int:
    """Wait until FILE contains TEXT."""
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        try:
            with open(args.file) as f:
                if args.text in f.read():
                    print(f'found: {args.text}')
                    return 0
        except FileNotFoundError:
            pass
        time.sleep(POLL_INTERVAL)
    print(f'not found: {args.text}')
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(required=True)

    p = subparsers.add_parser('hold', help=hold.__doc__)
    p.add_argument('host')
    p.add_argument('port', type=int)
    p.add_argument('count', type=int)
    p.set_defaults(func=hold)

    p = subparsers.add_parser('request', help=request.__doc__)
    p.add_argument('host')
    p.add_argument('port', type=int)
    p.add_argument('expect', choices=['served', 'refused'])
    p.add_argument('--tls', action='store_true', help='Use TLS.')
    p.add_argument('--proxy-protocol', metavar='SOURCE', help='Send a PROXY protocol header that claims this source address.')
    p.add_argument('--retry', type=float, default=0, help='How many seconds to retry for.')
    p.add_argument(
        '--metric',
        action='append',
        help='Print this metric while the connection is open, then wait for the proxy to close it. Repeat for more metrics.')
    p.set_defaults(func=request)

    p = subparsers.add_parser('wait-metric', help=wait_metric.__doc__)
    p.add_argument('metrics', nargs='+', metavar='NAME VALUE')
    p.set_defaults(func=wait_metric)

    p = subparsers.add_parser('wait-log', help=wait_log.__doc__)
    p.add_argument('file')
    p.add_argument('text')
    p.set_defaults(func=wait_log)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
