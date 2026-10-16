#!/usr/bin/env python3
'''
HTTP/2 client that floods a closed stream with DATA and then verifies the
connection is still usable.
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
from typing import Dict, List, Optional, Tuple

import hpack

CONNECTION_PREFACE = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'

FRAME_TYPE_DATA = 0
FRAME_TYPE_HEADERS = 1
FRAME_TYPE_RST_STREAM = 3
FRAME_TYPE_SETTINGS = 4
FRAME_TYPE_PING = 6
FRAME_TYPE_GOAWAY = 7
FRAME_TYPE_WINDOW_UPDATE = 8

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04

CONNECTION_STREAM_ID = 0
CLOSED_STREAM_ID = 1
PROBE_STREAM_ID = 3

MAX_FRAME_SIZE = 16384
INITIAL_WINDOW_SIZE = 65535

# Send enough DATA to the closed stream to consume the initial connection
# window twice over. An honest client can only do this if the peer credits the
# discarded bytes back with connection-level WINDOW_UPDATE frames.
CLOSED_STREAM_DATA_TOTAL = 2 * INITIAL_WINDOW_SIZE
PROBE_BODY = b'p' * 1024

# How long to wait for a WINDOW_UPDATE before concluding the connection window
# has been drained for good.
WINDOW_UPDATE_TIMEOUT_SECONDS = 3

Frame = Tuple[int, int, int, bytes]


class WindowDrained(Exception):
    """Raised when the peer never replenishes the connection window."""


class Http2Client:
    """A minimal HTTP/2 client that tracks the connection send window."""

    def __init__(self, port: int) -> None:
        self._sock = self._make_socket(port)
        self._encoder = hpack.Encoder()
        self._decoder = hpack.Decoder()
        self._send_window = INITIAL_WINDOW_SIZE
        self._window_update_count = 0
        self._window_update_total = 0
        self._rst_stream_count: Dict[int, int] = {}
        self._response_status: Dict[int, Optional[str]] = {}
        self._response_done: Dict[int, bool] = {}

    @staticmethod
    def _make_socket(port: int) -> ssl.SSLSocket:
        socket.setdefaulttimeout(10)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.set_alpn_protocols(['h2'])

        raw_socket = socket.create_connection(('127.0.0.1', port))
        tls_socket = ctx.wrap_socket(raw_socket, server_hostname='localhost')
        negotiated = tls_socket.selected_alpn_protocol()
        if negotiated != 'h2':
            raise RuntimeError(f'Expected h2 ALPN, negotiated {negotiated!r}')
        return tls_socket

    def close(self) -> None:
        self._sock.close()

    @property
    def window_update_count(self) -> int:
        return self._window_update_count

    @property
    def window_update_total(self) -> int:
        return self._window_update_total

    def rst_stream_count(self, stream_id: int) -> int:
        return self._rst_stream_count.get(stream_id, 0)

    def response_status(self, stream_id: int) -> Optional[str]:
        return self._response_status.get(stream_id)

    # Frame plumbing.

    @staticmethod
    def _make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> bytes:
        return len(payload).to_bytes(3, 'big') + bytes([frame_type, flags]) + (stream_id & 0x7fffffff).to_bytes(4, 'big') + payload

    def _send_frame(self, frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> None:
        self._sock.sendall(self._make_frame(frame_type, flags, stream_id, payload))

    def _read_exact(self, size: int) -> bytes:
        chunks = []
        remaining = size
        while remaining > 0:
            chunk = self._sock.recv(remaining)
            if not chunk:
                raise EOFError('socket closed')
            chunks.append(chunk)
            remaining -= len(chunk)
        return b''.join(chunks)

    def _read_frame(self) -> Frame:
        header = self._read_exact(9)
        length = int.from_bytes(header[0:3], 'big')
        frame_type = header[3]
        flags = header[4]
        stream_id = int.from_bytes(header[5:9], 'big') & 0x7fffffff
        payload = self._read_exact(length)
        return frame_type, flags, stream_id, payload

    def _handle_frame(self, frame: Frame) -> None:
        frame_type, flags, stream_id, payload = frame
        if frame_type == FRAME_TYPE_WINDOW_UPDATE and stream_id == CONNECTION_STREAM_ID:
            increment = int.from_bytes(payload[0:4], 'big') & 0x7fffffff
            self._send_window += increment
            self._window_update_count += 1
            self._window_update_total += increment
            print(f'connection WINDOW_UPDATE increment={increment} send_window={self._send_window}')
        elif frame_type == FRAME_TYPE_RST_STREAM:
            error_code = int.from_bytes(payload[0:4], 'big')
            self._rst_stream_count[stream_id] = self._rst_stream_count.get(stream_id, 0) + 1
            if self._rst_stream_count[stream_id] == 1:
                print(f'stream {stream_id} RST_STREAM error_code={error_code}')
        elif frame_type == FRAME_TYPE_GOAWAY:
            error_code = int.from_bytes(payload[4:8], 'big')
            raise RuntimeError(f'GOAWAY error_code={error_code}')
        elif frame_type == FRAME_TYPE_SETTINGS and not (flags & FLAG_ACK):
            self._send_frame(FRAME_TYPE_SETTINGS, FLAG_ACK)
        elif frame_type == FRAME_TYPE_PING and not (flags & FLAG_ACK):
            self._send_frame(FRAME_TYPE_PING, FLAG_ACK, CONNECTION_STREAM_ID, payload)
        elif frame_type == FRAME_TYPE_HEADERS:
            padded = flags & 0x08
            priority = flags & 0x20
            block = payload
            if padded:
                pad_length = block[0]
                block = block[1:len(block) - pad_length]
            if priority:
                block = block[5:]
            for name, value in self._decoder.decode(block):
                if name == ':status':
                    self._response_status[stream_id] = value
            if flags & FLAG_END_STREAM:
                self._response_done[stream_id] = True
        elif frame_type == FRAME_TYPE_DATA:
            # Keep our own receive windows open so responses never stall.
            if payload:
                increment = len(payload).to_bytes(4, 'big')
                self._send_frame(FRAME_TYPE_WINDOW_UPDATE, 0, CONNECTION_STREAM_ID, increment)
                if not (flags & FLAG_END_STREAM):
                    self._send_frame(FRAME_TYPE_WINDOW_UPDATE, 0, stream_id, increment)
            if flags & FLAG_END_STREAM:
                self._response_done[stream_id] = True

    def _read_and_handle_frame(self) -> None:
        self._handle_frame(self._read_frame())

    # Protocol steps.

    def handshake(self) -> None:
        self._sock.sendall(CONNECTION_PREFACE)
        self._send_frame(FRAME_TYPE_SETTINGS)
        while True:
            frame = self._read_frame()
            self._handle_frame(frame)
            if frame[0] == FRAME_TYPE_SETTINGS and not (frame[1] & FLAG_ACK):
                return

    def send_request_headers(self, stream_id: int, method: str, path: str, end_stream: bool, extra: List[Tuple[str, str]]) -> None:
        headers = [(':method', method), (':scheme', 'https'), (':authority', 'localhost'), (':path', path)] + extra
        flags = FLAG_END_HEADERS | (FLAG_END_STREAM if end_stream else 0)
        self._send_frame(FRAME_TYPE_HEADERS, flags, stream_id, self._encoder.encode(headers))

    def send_data(self, stream_id: int, payload: bytes, end_stream: bool) -> None:
        """Send DATA on stream_id, honoring the connection send window."""
        offset = 0
        while offset < len(payload):
            self._wait_for_send_window()
            chunk_size = min(MAX_FRAME_SIZE, self._send_window, len(payload) - offset)
            chunk = payload[offset:offset + chunk_size]
            offset += chunk_size
            flags = FLAG_END_STREAM if (end_stream and offset == len(payload)) else 0
            self._send_frame(FRAME_TYPE_DATA, flags, stream_id, chunk)
            self._send_window -= chunk_size

    def _wait_for_send_window(self) -> None:
        if self._send_window > 0:
            return
        self._sock.settimeout(WINDOW_UPDATE_TIMEOUT_SECONDS)
        try:
            while self._send_window <= 0:
                self._read_and_handle_frame()
        except (EOFError, socket.timeout) as exc:
            raise WindowDrained(f'send_window={self._send_window}: {exc!r}') from exc
        finally:
            self._sock.settimeout(10)

    def wait_for_response(self, stream_id: int) -> None:
        while not self._response_done.get(stream_id):
            self._read_and_handle_frame()


def run(port: int) -> int:
    """Verify closed-stream DATA does not permanently consume the connection window."""

    client = Http2Client(port)
    try:
        client.handshake()

        # Stream 1: a complete transaction, after which the stream is closed.
        client.send_request_headers(CLOSED_STREAM_ID, 'GET', '/get', True, [('uuid', 'closed-stream-window-1')])
        client.wait_for_response(CLOSED_STREAM_ID)
        print(f'stream {CLOSED_STREAM_ID} :status {client.response_status(CLOSED_STREAM_ID)}')

        # Flood the closed stream with DATA while honoring the connection window
        # as we understand it. If ATS charges these bytes without ever crediting
        # them back, the window reaches zero and this raises WindowDrained.
        try:
            client.send_data(CLOSED_STREAM_ID, b'x' * CLOSED_STREAM_DATA_TOTAL, False)
        except WindowDrained as exc:
            print(f'closed-stream DATA drained the connection window: {exc}')
            return 1
        print(
            f'closed-stream DATA sent={CLOSED_STREAM_DATA_TOTAL} '
            f'window_updates={client.window_update_count} credited={client.window_update_total}')

        # Stream 3: the connection must still be usable for a request with a body.
        client.send_request_headers(
            PROBE_STREAM_ID, 'POST', '/post', False, [('content-length', str(len(PROBE_BODY))), ('uuid', 'closed-stream-window-3')])
        try:
            client.send_data(PROBE_STREAM_ID, PROBE_BODY, True)
        except WindowDrained as exc:
            print(f'probe stream could not send its body: {exc}')
            return 1
        client.wait_for_response(PROBE_STREAM_ID)
        print(f'stream {PROBE_STREAM_ID} :status {client.response_status(PROBE_STREAM_ID)}')
        print(f'stream {CLOSED_STREAM_ID} RST_STREAM count={client.rst_stream_count(CLOSED_STREAM_ID)}')

        if client.response_status(PROBE_STREAM_ID) != '200':
            return 1
        if client.rst_stream_count(CLOSED_STREAM_ID) == 0:
            print('ATS did not reject any DATA on the closed stream')
            return 1
        return 0
    except (EOFError, socket.timeout, RuntimeError) as exc:
        print(f'connection failed: {exc!r}')
        return 1
    finally:
        client.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int, help='ATS TLS port')
    args = parser.parse_args()
    return run(args.port)


if __name__ == '__main__':
    raise SystemExit(main())
