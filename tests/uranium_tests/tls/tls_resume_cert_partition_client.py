#!/usr/bin/env python3
"""Offer a TLS session saved from one connection to a second connection.

Each connection sends no SNI, so the server chooses its certificate by the
destination address, optionally given in a PROXY protocol v1 header. For each
leg this prints the certificate the server presented and whether the session
was resumed.
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
import ipaddress
import socket
import ssl
import sys
from dataclasses import dataclass


@dataclass
class Leg:
    '''One connection: where to connect, which path to request, and an optional PROXY destination.'''
    host: str
    port: int
    path: str
    proxy_dst: str | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ca', action='append', required=True, help='CA file trusted for the server certificates.')
    parser.add_argument('--tls', choices=('1.2', '1.3'), required=True, help='TLS version to use.')
    for leg in ('first', 'second'):
        parser.add_argument(
            f'--{leg}', nargs=3, metavar=('HOST', 'PORT', 'PATH'), required=True, help=f'Where the {leg} connection goes.')
        parser.add_argument(f'--{leg}-proxy-dst', help=f'Send a PROXY header naming this destination on the {leg} connection.')
    return parser.parse_args()


def make_leg(args: argparse.Namespace, leg: str) -> Leg:
    '''Build a leg from its command line options.'''
    host, port, path = getattr(args, leg)
    return Leg(host, int(port), path, getattr(args, f'{leg}_proxy_dst'))


def make_context(cas: list[str], tls: str) -> ssl.SSLContext:
    '''Make a client context that sends no SNI but still verifies, so the peer's subject is reported.'''
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_REQUIRED
    for ca in cas:
        ctx.load_verify_locations(cafile=ca)
    version = ssl.TLSVersion.TLSv1_3 if tls == '1.3' else ssl.TLSVersion.TLSv1_2
    ctx.minimum_version = version
    ctx.maximum_version = version
    return ctx


def proxy_header(sock: socket.socket, leg: Leg) -> bytes:
    '''Build a PROXY protocol v1 header naming the leg's destination.'''
    src_host, src_port = sock.getsockname()[:2]
    dst = ipaddress.ip_address(leg.proxy_dst)
    src = ipaddress.ip_address(src_host)
    if src.version != dst.version:
        src = ipaddress.ip_address('127.0.0.1' if dst.version == 4 else '::1')
    family = 'TCP4' if dst.version == 4 else 'TCP6'
    return f'PROXY {family} {src} {dst} {src_port} {leg.port}\r\n'.encode()


def subject_cn(cert: dict) -> str:
    '''Return the common name of a certificate as returned by getpeercert().'''
    for rdn in cert.get('subject', ()):
        for key, value in rdn:
            if key == 'commonName':
                return value
    return '-'


def connect(ctx: ssl.SSLContext, leg: Leg, session: ssl.SSLSession | None) -> ssl.SSLSession | None:
    '''Make one request on a new connection, print what was observed, and return its session.'''
    raw = socket.create_connection((leg.host, leg.port), timeout=10)
    if leg.proxy_dst is not None:
        raw.sendall(proxy_header(raw, leg))
    with ctx.wrap_socket(raw, server_hostname=None, session=session) as tls:
        tls.sendall(f'GET {leg.path} HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n'.encode())
        response = b''
        while chunk := tls.recv(4096):
            response += chunk
        status = response.split(b'\r\n', 1)[0].decode(errors='replace')
        print(f'{leg.path}: CN={subject_cn(tls.getpeercert())} reused={tls.session_reused} status={status}', flush=True)
        # Reading to EOF lets a TLS 1.3 NewSessionTicket arrive before the session is taken.
        return tls.session


def main() -> int:
    args = parse_args()
    ctx = make_context(args.ca, args.tls)
    session = connect(ctx, make_leg(args, 'first'), None)
    if session is None:
        print('ERROR: the first connection yielded no session', file=sys.stderr)
        return 1
    connect(ctx, make_leg(args, 'second'), session)
    return 0


if __name__ == '__main__':
    sys.exit(main())
