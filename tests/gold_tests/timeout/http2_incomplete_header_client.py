#!/usr/bin/env python3
'''
HTTP/2 client that fragments a request header block across a HEADERS frame and
an optional CONTINUATION frame in order to exercise
proxy.config.http2.incomplete_header_timeout_in.
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
import time
from typing import List, Optional, Tuple

import hpack

CONNECTION_PREFACE = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'

FRAME_TYPE_DATA = 0x00
FRAME_TYPE_HEADERS = 0x01
FRAME_TYPE_RST_STREAM = 0x03
FRAME_TYPE_SETTINGS = 0x04
FRAME_TYPE_GOAWAY = 0x07
FRAME_TYPE_CONTINUATION = 0x09

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04

STREAM_ID = 1


def make_socket(port: int, socket_timeout: float) -> ssl.SSLSocket:
    '''Establish a TLS connection to ATS with h2 negotiated via ALPN.

    :param port: The ATS TLS port to connect to.
    :param socket_timeout: The socket timeout, in seconds.
    :return: The connected TLS socket.
    '''
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])

    raw = socket.create_connection(('127.0.0.1', port), timeout=socket_timeout)
    tls = ctx.wrap_socket(raw, server_hostname='localhost')
    if tls.selected_alpn_protocol() != 'h2':
        raise RuntimeError(f'failed to negotiate h2, got {tls.selected_alpn_protocol()!r}')
    return tls


def make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> bytes:
    '''Serialize an HTTP/2 frame.

    :param frame_type: The frame type.
    :param flags: The frame flags.
    :param stream_id: The stream the frame belongs to.
    :param payload: The frame payload.
    :return: The serialized frame.
    '''
    return (len(payload).to_bytes(3, 'big') + bytes([frame_type, flags]) + (stream_id & 0x7fffffff).to_bytes(4, 'big') + payload)


def read_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    '''Read exactly @a size bytes from @a sock.

    :param sock: The socket to read from.
    :param size: The number of bytes to read.
    :return: The bytes read.
    '''
    chunks: List[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError('socket closed')
        chunks.append(chunk)
        remaining -= len(chunk)
    return b''.join(chunks)


def read_frame(sock: ssl.SSLSocket) -> Tuple[int, int, int, bytes]:
    '''Read a single HTTP/2 frame from @a sock.

    :param sock: The socket to read from.
    :return: The frame type, flags, stream id, and payload.
    '''
    header = read_exact(sock, 9)
    length = int.from_bytes(header[0:3], 'big')
    frame_type = header[3]
    flags = header[4]
    stream_id = int.from_bytes(header[5:9], 'big') & 0x7fffffff
    payload = read_exact(sock, length)
    return frame_type, flags, stream_id, payload


def complete_handshake(sock: ssl.SSLSocket) -> None:
    '''Exchange the connection preface and SETTINGS frames.

    The handshake is completed before the request is sent so that the client
    does not have to interleave reads with the deliberate delay before the
    CONTINUATION frame.

    :param sock: The socket to handshake on.
    '''
    sock.sendall(CONNECTION_PREFACE)
    sock.sendall(make_frame(FRAME_TYPE_SETTINGS))

    acked_peer_settings = False
    received_settings_ack = False
    while not (acked_peer_settings and received_settings_ack):
        frame_type, flags, _, _ = read_frame(sock)
        if frame_type != FRAME_TYPE_SETTINGS:
            continue
        if flags & FLAG_ACK:
            received_settings_ack = True
        else:
            sock.sendall(make_frame(FRAME_TYPE_SETTINGS, FLAG_ACK, 0))
            acked_peer_settings = True


def request_block(path: str, uuid: str) -> bytes:
    '''HPACK encode the request header block.

    :param path: The value for the :path pseudo header.
    :param uuid: The value for the uuid header that the Proxy Verifier server
      keys transactions off of.
    :return: The encoded header block.
    '''
    return hpack.Encoder().encode(
        [
            (':method', 'GET'),
            (':scheme', 'https'),
            (':authority', 'example.com'),
            (':path', path),
            ('uuid', uuid),
        ])


def read_until_stream_ends(sock: ssl.SSLSocket, start: float) -> bool:
    '''Read frames until ATS ends the stream or the connection.

    :param sock: The socket to read from.
    :param start: The monotonic time at which the request HEADERS frame was sent.
    :return: Whether a response status was received for the request.
    '''
    decoder = hpack.Decoder()
    header_fragments: List[bytes] = []
    got_status = False

    while True:
        frame_type, flags, stream_id, payload = read_frame(sock)

        # Nothing is written back here. While ATS is waiting for a CONTINUATION
        # frame, any other frame from the client -- a SETTINGS ACK included --
        # is a connection level PROTOCOL_ERROR, which would mask the timeout
        # under test.
        if frame_type == FRAME_TYPE_GOAWAY:
            last_stream_id = int.from_bytes(payload[0:4], 'big') & 0x7fffffff
            error_code = int.from_bytes(payload[4:8], 'big')
            print(f'GOAWAY error_code={error_code} last_stream_id={last_stream_id} '
                  f'elapsed={time.monotonic() - start:.2f}')
            return got_status

        if stream_id != STREAM_ID:
            continue

        if frame_type == FRAME_TYPE_RST_STREAM:
            error_code = int.from_bytes(payload[0:4], 'big')
            print(f'stream {stream_id}: RST_STREAM error_code={error_code} elapsed={time.monotonic() - start:.2f}')
            return got_status

        if frame_type in (FRAME_TYPE_HEADERS, FRAME_TYPE_CONTINUATION):
            header_fragments.append(payload)
            if flags & FLAG_END_HEADERS:
                fields = dict(decoder.decode(b''.join(header_fragments)))
                header_fragments = []
                status = fields.get(':status')
                if status is not None:
                    got_status = True
                    print(f'stream {stream_id}: status={status} elapsed={time.monotonic() - start:.2f}')

        if (flags & FLAG_END_STREAM) and frame_type in (FRAME_TYPE_DATA, FRAME_TYPE_HEADERS):
            print(f'stream {stream_id}: END_STREAM elapsed={time.monotonic() - start:.2f}')
            return got_status


def check_elapsed(elapsed: float, min_elapsed: Optional[float], max_elapsed: Optional[float]) -> bool:
    '''Verify that ATS ended the stream within the expected window.

    :param elapsed: How long ATS took to end the stream, in seconds.
    :param min_elapsed: The lower bound, or None for no lower bound.
    :param max_elapsed: The upper bound, or None for no upper bound.
    :return: Whether @a elapsed is within the expected window.
    '''
    if min_elapsed is not None and elapsed < min_elapsed:
        print(f'FAIL: ATS ended the stream after {elapsed:.2f}s, sooner than the expected {min_elapsed}s', file=sys.stderr)
        return False
    if max_elapsed is not None and elapsed > max_elapsed:
        print(f'FAIL: ATS ended the stream after {elapsed:.2f}s, later than the expected {max_elapsed}s', file=sys.stderr)
        return False
    print(f'timing ok: elapsed={elapsed:.2f}')
    return True


def run(args: argparse.Namespace) -> int:
    '''Send the request and report what ATS does with the stream.

    :param args: The parsed command line arguments.
    :return: The process exit code.
    '''
    block = request_block(args.path, args.uuid)
    print(f'end_headers={args.end_headers} continuation_delay={args.continuation_delay}')

    with make_socket(args.port, args.socket_timeout) as sock:
        complete_handshake(sock)

        flags = FLAG_END_STREAM
        fragment = block
        if args.end_headers:
            flags |= FLAG_END_HEADERS
        else:
            # Withholding END_HEADERS leaves ATS waiting for a CONTINUATION
            # frame, which is what incomplete_header_timeout_in bounds.
            fragment = block[:len(block) // 2]
        start = time.monotonic()
        sock.sendall(make_frame(FRAME_TYPE_HEADERS, flags, STREAM_ID, fragment))

        if args.continuation_delay is not None:
            time.sleep(args.continuation_delay)
            print(f'sending CONTINUATION elapsed={time.monotonic() - start:.2f}')
            sock.sendall(make_frame(FRAME_TYPE_CONTINUATION, FLAG_END_HEADERS, STREAM_ID, block[len(block) // 2:]))

        try:
            got_status = read_until_stream_ends(sock, start)
        except EOFError:
            print(f'EOF elapsed={time.monotonic() - start:.2f}')
            got_status = False
        except socket.timeout:
            print(f'FAIL: no response from ATS within the {args.socket_timeout}s socket timeout', file=sys.stderr)
            return 1

    elapsed = time.monotonic() - start
    print(f'got_status={got_status}')
    return 0 if check_elapsed(elapsed, args.min_elapsed, args.max_elapsed) else 1


def parse_args() -> argparse.Namespace:
    '''Parse the command line arguments.

    :return: The parsed arguments.
    '''
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='ATS TLS port')
    parser.add_argument('--path', default='/incomplete', help='request :path')
    parser.add_argument('--uuid', default='incomplete_header', help='request uuid header')
    parser.add_argument(
        '--end-headers', action='store_true', help='send the complete header block with END_HEADERS in a single HEADERS frame')
    parser.add_argument(
        '--continuation-delay',
        type=float,
        default=None,
        help='seconds after the HEADERS frame to send the END_HEADERS CONTINUATION frame; omit to never send it')
    parser.add_argument('--min-elapsed', type=float, default=None, help='fail if ATS ends the stream sooner than this')
    parser.add_argument('--max-elapsed', type=float, default=None, help='fail if ATS ends the stream later than this')
    parser.add_argument('--socket-timeout', type=float, default=60.0, help='socket timeout in seconds')
    return parser.parse_args()


def main() -> int:
    return run(parse_args())


if __name__ == '__main__':
    raise SystemExit(main())
