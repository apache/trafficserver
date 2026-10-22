#!/usr/bin/env python3
'''
HTTP/2 client that withholds SETTINGS ACKs while opening streams.
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
import time
from typing import Optional, Tuple

CONNECTION_PREFACE = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'

FRAME_TYPE_DATA = 0
FRAME_TYPE_HEADERS = 1
FRAME_TYPE_RST_STREAM = 3
FRAME_TYPE_SETTINGS = 4
FRAME_TYPE_GOAWAY = 7

FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04

ERROR_SETTINGS_TIMEOUT = 4


def make_socket(port: int) -> ssl.SSLSocket:
    """Create a TLS-wrapped HTTP/2 socket."""

    socket.setdefaulttimeout(15)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])

    raw_socket = socket.create_connection(('localhost', port))
    tls_socket = ctx.wrap_socket(raw_socket, server_hostname='localhost')
    negotiated = tls_socket.selected_alpn_protocol()
    if negotiated != 'h2':
        raise RuntimeError(f'Expected h2 ALPN, negotiated {negotiated!r}')
    return tls_socket


def make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> bytes:
    """Serialize a minimal HTTP/2 frame."""

    return (len(payload).to_bytes(3, 'big') + bytes([frame_type, flags]) + (stream_id & 0x7fffffff).to_bytes(4, 'big') + payload)


def read_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    """Read exactly @a size bytes or raise EOFError."""

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
    """Read and parse one HTTP/2 frame."""

    header = read_exact(sock, 9)
    length = int.from_bytes(header[0:3], 'big')
    frame_type = header[3]
    flags = header[4]
    stream_id = int.from_bytes(header[5:9], 'big') & 0x7fffffff
    payload = read_exact(sock, length)
    return frame_type, flags, stream_id, payload


def hpack_string(value: str) -> bytes:
    """Encode a short, non-Huffman HPACK string literal."""

    encoded = value.encode('utf-8')
    if len(encoded) >= 128:
        raise ValueError('test header value is too long for this helper')
    return bytes([len(encoded)]) + encoded


def hpack_literal_header(name: str, value: str) -> bytes:
    """Encode a non-indexed HPACK literal header with a new name."""

    return b'\x00' + hpack_string(name) + hpack_string(value)


def request_header_block(path: str, stream_id: int) -> bytes:
    """Build a tiny HPACK request block without using automatic SETTINGS ACKs."""

    block = bytearray()
    block.append(0x82)  # :method: GET
    block.append(0x87)  # :scheme: https
    block.append(0x01)  # :authority literal without indexing, indexed name 1
    block.extend(hpack_string('localhost'))
    if path == '/':
        block.append(0x84)  # :path: /
    else:
        block.append(0x04)  # :path literal without indexing, indexed name 4
        block.extend(hpack_string(path))
    block.extend(hpack_literal_header('uuid', f'settings-ack-stall-{stream_id}'))
    return bytes(block)


def send_request(sock: ssl.SSLSocket, stream_id: int) -> None:
    """Send one GET request with END_STREAM set."""

    flags = FLAG_END_HEADERS | FLAG_END_STREAM
    path = f'/stream/{stream_id}'
    sock.sendall(make_frame(FRAME_TYPE_HEADERS, flags, stream_id, request_header_block(path, stream_id)))


def read_until_streams_end(sock: ssl.SSLSocket, stream_ids: set[int]) -> Optional[int]:
    """Read frames until @a stream_ids end, returning a GOAWAY error if seen."""

    ended: set[int] = set()
    while ended != stream_ids:
        frame_type, flags, stream_id, payload = read_frame(sock)
        if frame_type == FRAME_TYPE_GOAWAY:
            error_code = int.from_bytes(payload[4:8], 'big')
            print(f'GOAWAY error_code={error_code}')
            return error_code
        if stream_id in stream_ids:
            if frame_type in (FRAME_TYPE_DATA, FRAME_TYPE_HEADERS) and flags & FLAG_END_STREAM:
                ended.add(stream_id)
            elif frame_type == FRAME_TYPE_RST_STREAM:
                ended.add(stream_id)
        elif frame_type == FRAME_TYPE_SETTINGS:
            # Deliberately do nothing. This test is about withholding SETTINGS
            # ACKs while continuing to read the connection.
            pass
    return None


def run(port: int) -> int:
    """Open enough stream waves to exhaust ATS's outstanding SETTINGS cap."""

    with make_socket(port) as sock:
        sock.sendall(CONNECTION_PREFACE)
        sock.sendall(make_frame(FRAME_TYPE_SETTINGS))

        next_stream_id = 1
        for _ in range(5):
            stream_ids = {next_stream_id, next_stream_id + 2}
            for stream_id in sorted(stream_ids):
                send_request(sock, stream_id)
            next_stream_id += 4

            error_code = read_until_streams_end(sock, stream_ids)
            if error_code is not None:
                return 0 if error_code == ERROR_SETTINGS_TIMEOUT else 1
            # Give ATS a moment to retire the completed streams before opening the next wave.
            time.sleep(0.05)

    print('Expected SETTINGS_TIMEOUT GOAWAY, but the connection closed first')
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int, help='ATS TLS port')
    args = parser.parse_args()
    return run(args.port)


if __name__ == '__main__':
    raise SystemExit(main())
