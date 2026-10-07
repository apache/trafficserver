#!/usr/bin/env python3
'''
Send a Chrome-shaped TLS ClientHello and report the JA4 fingerprint it should produce.

The ClientHello carries GREASE cipher suites and extensions as well as the
ALPS (0x4469) extension, which OpenSSL does not recognize. A fingerprint
computed from the extensions OpenSSL reports would miss all of these.
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
import hashlib
import os
import socket
import struct
import sys

SERVER_NAME = b'jax.server.test'

GREASE_CIPHER = 0x0A0A
CIPHERS = [
    GREASE_CIPHER, 0x1301, 0x1302, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC030, 0xCCA9, 0xCCA8, 0xC013, 0xC014, 0x009C, 0x009D, 0x002F,
    0x0035
]

SIGNATURE_ALGORITHMS = [0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601]

EXT_SERVER_NAME = 0x0000
EXT_ALPN = 0x0010
EXT_ALPS = 0x4469
FIRST_GREASE_EXTENSION = 0x2A2A
LAST_GREASE_EXTENSION = 0x9A9A


def is_grease(value: int) -> bool:
    return (value & 0x0F0F) == 0x0A0A and (value >> 8) == (value & 0xFF)


def u8_prefixed(data: bytes) -> bytes:
    return struct.pack('!B', len(data)) + data


def u16_prefixed(data: bytes) -> bytes:
    return struct.pack('!H', len(data)) + data


def u16_list(values: list[int]) -> bytes:
    return b''.join(struct.pack('!H', v) for v in values)


def extensions() -> list[tuple[int, bytes]]:
    '''Return the ClientHello extensions, in Chrome's wire order.'''
    server_name = u16_prefixed(b'\x00' + u16_prefixed(SERVER_NAME))
    alpn = u16_prefixed(u8_prefixed(b'h2') + u8_prefixed(b'http/1.1'))
    groups = u16_prefixed(u16_list([0x1A1A, 0x001D, 0x0017, 0x0018]))
    key_share = u16_prefixed(u16_list([0x1A1A]) + u16_prefixed(b'\x00') + u16_list([0x001D]) + u16_prefixed(os.urandom(32)))
    supported_versions = u8_prefixed(u16_list([0x3A3A, 0x0304, 0x0303]))
    return [
        (FIRST_GREASE_EXTENSION, b''),
        (EXT_SERVER_NAME, server_name),
        (0x0017, b''),  # extended_master_secret
        (0xFF01, b'\x00'),  # renegotiation_info
        (0x000A, groups),  # supported_groups
        (0x000B, u8_prefixed(b'\x00')),  # ec_point_formats
        (0x0023, b''),  # session_ticket
        (EXT_ALPN, alpn),
        (0x0005, b'\x01\x00\x00\x00\x00'),  # status_request
        (0x000D, u16_prefixed(u16_list(SIGNATURE_ALGORITHMS))),  # signature_algorithms
        (0x0012, b''),  # signed_certificate_timestamp
        (0x0033, key_share),  # key_share
        (0x002D, u8_prefixed(b'\x01')),  # psk_key_exchange_modes
        (0x002B, supported_versions),  # supported_versions
        (0x001B, u8_prefixed(u16_list([0x0002]))),  # compress_certificate
        (EXT_ALPS, u16_prefixed(u8_prefixed(b'h2'))),  # application_settings
        (LAST_GREASE_EXTENSION, b'\x00'),
        (0x0015, bytes(16)),  # padding
    ]


def client_hello_record() -> bytes:
    '''Return a TLS record holding the ClientHello.'''
    body = struct.pack('!H', 0x0303) + os.urandom(32) + u8_prefixed(os.urandom(32))
    body += u16_prefixed(u16_list(CIPHERS))
    body += u8_prefixed(b'\x00')
    body += u16_prefixed(b''.join(struct.pack('!HH', t, len(d)) + d for t, d in extensions()))
    handshake = b'\x01' + struct.pack('!I', len(body))[1:] + body
    return struct.pack('!BHH', 0x16, 0x0301, len(handshake)) + handshake


def truncated_sha256(values: list[str]) -> str:
    return hashlib.sha256(','.join(values).encode()).hexdigest()[:12]


def expected_ja4() -> str:
    '''Return the JA4 fingerprint computed from every extension on the wire.

    The c section hashes the signature algorithms per the JA4 specification.
    '''
    ciphers = [c for c in CIPHERS if not is_grease(c)]
    types = [t for t, _ in extensions() if not is_grease(t)]
    hashed_types = [t for t in types if t not in (EXT_SERVER_NAME, EXT_ALPN)]

    part_a = f't13d{len(ciphers):02d}{len(types):02d}h2'
    part_b = truncated_sha256(sorted(f'{c:04x}' for c in ciphers))
    sigalgs = ','.join(f'{a:04x}' for a in SIGNATURE_ALGORITHMS)
    part_c = hashlib.sha256(f"{','.join(sorted(f'{t:04x}' for t in hashed_types))}_{sigalgs}".encode()).hexdigest()[:12]
    return f'{part_a}_{part_b}_{part_c}'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='The TLS port to send the ClientHello to.')
    args = parser.parse_args()

    with socket.create_connection(('127.0.0.1', args.port), timeout=5) as sock:
        sock.sendall(client_hello_record())
        # Wait for the ServerHello so that the ClientHello has been processed.
        response = sock.recv(5)
    if len(response) < 1 or response[0] != 0x16:
        print(f'Unexpected response to the ClientHello: {response!r}', file=sys.stderr)
        return 1
    print(f'Expected JA4: {expected_ja4()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
