#!/usr/bin/env python3
'''
Download an object from ATS over TLS and inspect the TLS records on the wire:
confirm the body arrives intact AND that application-data records follow the
configured fixed or dynamic record-size strategy.

A MemoryBIO drives the handshake so the raw ciphertext stream is visible; the
5-byte TLS record headers (type, version, length) are in cleartext, so record
sizes can be measured without decrypting. TLS 1.2 is pinned so that handshake
messages are their own record type (22) and only real application data is type 23,
and the cipher is restricted to AEAD (GCM) suites so the per-record overhead is a
small fixed constant rather than the variable IV/MAC/padding of a CBC suite.
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
from collections.abc import Iterator

TLS_APPLICATION_DATA = 23
TLS12_GCM_OVERHEAD = 24
# A clamped plaintext record becomes ciphertext of plaintext + AEAD overhead
# (TLS1.2 GCM: 8-byte explicit nonce + 16-byte tag = 24 bytes; the cipher is pinned
# to AEAD below). 256 is a generous ceiling over that, far below an unclamped ~16 KB
# record, so the clamp check stays decisive and cannot be tripped by the larger,
# variable expansion of a CBC suite.
RECORD_OVERHEAD = 256
DYNAMIC_SMALL_RECORD = 1300
DYNAMIC_MAX_RECORD = 16383
DYNAMIC_BYTE_THRESHOLD = 1_000_000


def iter_record_lengths(buf: bytes | bytearray) -> Iterator[tuple[int, int]]:
    '''Yield (content_type, record_length) for each complete TLS record in buf.'''
    i, n = 0, len(buf)
    while i + 5 <= n:
        content_type = buf[i]
        length = (buf[i + 3] << 8) | buf[i + 4]
        if i + 5 + length > n:
            break  # truncated trailing record
        yield content_type, length
        i += 5 + length


def verify_dynamic_records(app_lengths: list[int]) -> bool:
    '''Verify records ramp from single-segment to maximum-sized records.'''
    small_limit = DYNAMIC_SMALL_RECORD + TLS12_GCM_OVERHEAD
    max_limit = DYNAMIC_MAX_RECORD + TLS12_GCM_OVERHEAD
    first_large = next((i for i, length in enumerate(app_lengths) if length > small_limit), None)

    if first_large is None:
        print('FAIL: dynamic sizing never ramped up to large TLS records')
        return False

    plaintext_before_ramp = sum(length - TLS12_GCM_OVERHEAD for length in app_lengths[:first_large])
    if plaintext_before_ramp < DYNAMIC_BYTE_THRESHOLD:
        print(
            f'FAIL: dynamic sizing ramped after only {plaintext_before_ramp} plaintext bytes; '
            f'expected at least {DYNAMIC_BYTE_THRESHOLD}')
        return False

    max_record = max(app_lengths)
    if max_record > max_limit:
        print(f'FAIL: a dynamic application-data record ({max_record}) exceeds the maximum ({max_limit})')
        return False

    print(
        f'PASS: TLS records ramp from small to large after the dynamic threshold '
        f'(first_large={first_large}, plaintext_before_ramp={plaintext_before_ramp}, max_record_len={max_record})')
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description='Measure ATS TLS record sizes on a download.')
    parser.add_argument('-p', '--port', type=int, required=True, help='ATS TLS port')
    parser.add_argument('--host', default='ex.test', help='Host header / SNI')
    parser.add_argument('--path', default='/obj', help='request path')
    sizing = parser.add_mutually_exclusive_group(required=True)
    sizing.add_argument('--max-record', type=int, help='positive proxy.config.ssl.max_record_size clamp')
    sizing.add_argument('--dynamic', action='store_true', help='expect dynamic TLS record sizing')
    parser.add_argument('--expect-bytes', type=int, required=True, help='expected response body length')
    args = parser.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    # Pin AEAD (GCM) suites: their per-record overhead is a small fixed 24 bytes, so a
    # clamped record stays well within max_record + RECORD_OVERHEAD. A negotiated CBC
    # suite could expand a record by IV + MAC + up to 255 bytes of padding and trip the
    # clamp check spuriously. SECLEVEL=0 keeps the small test cert usable.
    ctx.set_ciphers(
        'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:'
        'ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:'
        'AES128-GCM-SHA256:AES256-GCM-SHA384@SECLEVEL=0')

    incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
    tls = ctx.wrap_bio(incoming, outgoing, server_hostname=args.host)

    sock = socket.create_connection(('127.0.0.1', args.port), timeout=30)
    sock.settimeout(30)
    raw = bytearray()  # every ciphertext byte the server sends, in order

    def flush() -> None:
        data = outgoing.read()
        if data:
            sock.sendall(data)

    def feed() -> bytes:
        chunk = sock.recv(65536)
        if chunk:
            raw.extend(chunk)
            incoming.write(chunk)
        return chunk

    while True:
        try:
            tls.do_handshake()
            break
        except ssl.SSLWantReadError:
            flush()
            if not feed():
                print('FAIL: server closed during handshake')
                return 2
    flush()

    request = (f'GET {args.path} HTTP/1.1\r\n'
               f'Host: {args.host}\r\n'
               f'Connection: close\r\n\r\n').encode()
    tls.write(request)
    flush()

    response = bytearray()
    while True:
        try:
            data = tls.read(65536)
            if not data:
                break  # clean close_notify
            response.extend(data)
        except ssl.SSLWantReadError:
            flush()
            if not feed():
                break  # socket closed by ATS
        except ssl.SSLEOFError:
            break

    separator = response.find(b'\r\n\r\n')
    body_len = len(response) - (separator + 4) if separator >= 0 else -1

    app_lengths = [length for content_type, length in iter_record_lengths(raw) if content_type == TLS_APPLICATION_DATA]
    max_record = max(app_lengths) if app_lengths else 0

    print(f'app_data_records={len(app_lengths)} max_record_len={max_record} body_len={body_len} expect={args.expect_bytes}')

    if body_len != args.expect_bytes:
        print(f'FAIL: body length {body_len} != expected {args.expect_bytes}')
        return 1
    if len(app_lengths) < 2:
        print('FAIL: too few application-data records to judge clamping')
        return 1
    if args.dynamic:
        return 0 if verify_dynamic_records(app_lengths) else 1

    assert args.max_record is not None
    limit = args.max_record + RECORD_OVERHEAD
    if max_record > limit:
        print(f'FAIL: an application-data record ({max_record}) exceeds the clamp + overhead ({limit})')
        return 1
    print('PASS: every application-data record is within the configured clamp')
    return 0


if __name__ == '__main__':
    sys.exit(main())
