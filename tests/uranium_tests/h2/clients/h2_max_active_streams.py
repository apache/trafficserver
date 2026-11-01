#!/usr/bin/env python3
'''
HTTP/2 client that opens N concurrent streams to test the global
active-streams cap and the HPACK dynamic-table sync across refused streams.
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
from typing import Dict, Optional, Set, Tuple

import hpack

CONNECTION_PREFACE = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'

FRAME_TYPE_DATA = 0x00
FRAME_TYPE_HEADERS = 0x01
FRAME_TYPE_RST_STREAM = 0x03
FRAME_TYPE_SETTINGS = 0x04
FRAME_TYPE_GOAWAY = 0x07

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04

ERROR_REFUSED_STREAM = 0x07

# Reusable header value carried on streams >= the cap. Encoding the same
# (name, value) pair on a later stream forces hpack to emit an indexed
# reference into the dynamic-table entry added by the earlier encode, which
# only resolves correctly if ATS decoded the earlier (refused) HEADERS frame.
PROBE_HEADER_NAME = 'x-test-probe'
PROBE_HEADER_VALUE = 'shared-probe-value'


def make_socket(port: int) -> ssl.SSLSocket:
    socket.setdefaulttimeout(15)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])

    raw = socket.create_connection(('127.0.0.1', port))
    tls = ctx.wrap_socket(raw, server_hostname='localhost')
    if tls.selected_alpn_protocol() != 'h2':
        raise RuntimeError(f'failed to negotiate h2, got {tls.selected_alpn_protocol()!r}')
    return tls


def make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> bytes:
    return (len(payload).to_bytes(3, 'big') + bytes([frame_type, flags]) + (stream_id & 0x7fffffff).to_bytes(4, 'big') + payload)


def read_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError('socket closed')
        chunks.append(chunk)
        remaining -= len(chunk)
    return b''.join(chunks)


def read_frame(sock: ssl.SSLSocket) -> Tuple[int, int, int, bytes]:
    header = read_exact(sock, 9)
    length = int.from_bytes(header[0:3], 'big')
    frame_type = header[3]
    flags = header[4]
    stream_id = int.from_bytes(header[5:9], 'big') & 0x7fffffff
    payload = read_exact(sock, length)
    return frame_type, flags, stream_id, payload


def request_block(encoder: hpack.Encoder, stream_id: int, include_probe: bool) -> bytes:
    headers = [
        (':method', 'GET'),
        (':scheme', 'https'),
        (':authority', 'www.example.com'),
        (':path', f'/stream/{stream_id}'),
        ('uuid', f'max-active-streams-{stream_id}'),
    ]
    if include_probe:
        headers.append((PROBE_HEADER_NAME, PROBE_HEADER_VALUE))
    return encoder.encode(headers)


def run(port: int, num_streams: int, probe_from: Optional[int]) -> int:
    """Open @a num_streams concurrent streams.

    Returns 0 on success.

    Streams whose id is >= @a probe_from carry a fixed (name, value) header
    so the second and later occurrences are encoded as indexed references
    into the dynamic table established by the first occurrence. If ATS
    fails to keep its decoder dynamic table in sync after refusing a
    stream, the indexed reference fails to resolve and ATS sends a
    COMPRESSION_ERROR GOAWAY, which this script reports as a failure.
    """

    stream_ids = [1 + 2 * i for i in range(num_streams)]
    encoder = hpack.Encoder()
    with make_socket(port) as sock:
        sock.sendall(CONNECTION_PREFACE)
        sock.sendall(make_frame(FRAME_TYPE_SETTINGS))
        for sid in stream_ids:
            include_probe = probe_from is not None and sid >= probe_from
            block = request_block(encoder, sid, include_probe)
            sock.sendall(make_frame(FRAME_TYPE_HEADERS, FLAG_END_HEADERS | FLAG_END_STREAM, sid, block))

        ended: Set[int] = set()
        statuses: Dict[int, str] = {}
        try:
            while ended != set(stream_ids):
                frame_type, flags, stream_id, payload = read_frame(sock)
                if frame_type == FRAME_TYPE_SETTINGS and not (flags & FLAG_ACK):
                    sock.sendall(make_frame(FRAME_TYPE_SETTINGS, FLAG_ACK, 0))
                    continue
                if frame_type == FRAME_TYPE_GOAWAY:
                    error_code = int.from_bytes(payload[4:8], 'big')
                    print(f'GOAWAY error_code={error_code}')
                    return 1
                if stream_id in stream_ids:
                    if frame_type == FRAME_TYPE_RST_STREAM:
                        error_code = int.from_bytes(payload[0:4], 'big')
                        statuses[stream_id] = f'rst:{error_code}'
                        print(f'stream {stream_id}: RST_STREAM error_code={error_code}')
                        ended.add(stream_id)
                    elif frame_type in (FRAME_TYPE_DATA, FRAME_TYPE_HEADERS) and (flags & FLAG_END_STREAM):
                        statuses[stream_id] = 'end_stream'
                        print(f'stream {stream_id}: END_STREAM')
                        ended.add(stream_id)
        except (EOFError, socket.timeout) as exc:
            print(f'socket terminated before all streams ended: {exc}', file=sys.stderr)
            return 1

    for sid, status in sorted(statuses.items()):
        print(f'final stream {sid}: {status}')
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int, help='ATS TLS port')
    parser.add_argument('--streams', type=int, default=4, help='number of concurrent streams to open')
    parser.add_argument(
        '--probe-from',
        type=int,
        default=None,
        help='lowest stream id (odd) to carry the shared probe header; omit to disable probe')
    args = parser.parse_args()
    return run(args.port, args.streams, args.probe_from)


if __name__ == '__main__':
    raise SystemExit(main())
