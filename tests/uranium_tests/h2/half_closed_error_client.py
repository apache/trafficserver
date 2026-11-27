#!/usr/bin/env python3
'''
HTTP/2 client, with its own HTTP/1.1 origin, that causes a connection error on
a session ATS has already half-closed with a graceful-shutdown GOAWAY. When
the error arrives, a DATA frame ATS queued without a flush is still in its
write buffer, and no new GOAWAY is sent that would flush it.
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
from pathlib import Path
import re
import socket
import ssl
import sys
import threading
import time
from typing import List, Tuple

import hpack

CONNECTION_PREFACE = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'

FRAME_TYPE_DATA = 0x00
FRAME_TYPE_HEADERS = 0x01
FRAME_TYPE_SETTINGS = 0x04
FRAME_TYPE_PING = 0x06
FRAME_TYPE_GOAWAY = 0x07

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04

STREAM_ID = 1

# The body parts the origin sends. The last one is never sent, so the stream
# is still open when the connection error arrives.
FIRST_PART = b'first-part;'
SECOND_PART = b'second-part;'
LAST_PART = b'last-part;'


def make_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b'') -> bytes:
    '''Serialize an HTTP/2 frame.

    :param frame_type: The frame type.
    :param flags: The frame flags.
    :param stream_id: The stream the frame belongs to.
    :param payload: The frame payload.
    :return: The serialized frame.
    '''
    return len(payload).to_bytes(3, 'big') + bytes([frame_type, flags]) + (stream_id & 0x7FFFFFFF).to_bytes(4, 'big') + payload


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
    stream_id = int.from_bytes(header[5:9], 'big') & 0x7FFFFFFF
    payload = read_exact(sock, length)
    return frame_type, flags, stream_id, payload


class Origin:
    '''An HTTP/1.1 origin that sends its response body in parts on cue.'''

    def __init__(self, port: int, timeout: float):
        '''Listen on @a port.

        :param port: The port to listen on.
        :param timeout: How long, in seconds, to wait for each cue.
        '''
        self.timeout = timeout
        self.send_second_part = threading.Event()
        self.second_part_sent = threading.Event()
        self.done = threading.Event()
        self.error: str | None = None
        self._listener = socket.create_server(('127.0.0.1', port))
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def start(self) -> None:
        '''Start serving one connection.'''
        self._thread.start()

    def join(self) -> None:
        '''Let the origin finish and wait for it.'''
        self.done.set()
        self._thread.join(self.timeout)
        self._listener.close()

    def _serve(self) -> None:
        '''Answer one request: headers and the first part now, the second on cue.'''
        try:
            self._listener.settimeout(self.timeout)
            conn, _ = self._listener.accept()
            with conn:
                conn.settimeout(self.timeout)
                request = b''
                while b'\r\n\r\n' not in request:
                    data = conn.recv(4096)
                    if not data:
                        raise EOFError('ATS closed the origin connection before sending a request')
                    request += data
                length = len(FIRST_PART) + len(SECOND_PART) + len(LAST_PART)
                conn.sendall(f'HTTP/1.1 200 OK\r\nContent-Length: {length}\r\n\r\n'.encode() + FIRST_PART)
                if not self.send_second_part.wait(self.timeout):
                    raise TimeoutError('no cue to send the second part')
                conn.sendall(SECOND_PART)
                self.second_part_sent.set()
                self.done.wait(self.timeout)
        except Exception as e:
            self.error = f'{type(e).__name__}: {e}'
        finally:
            # Set on failure too, so check error after waiting on it.
            self.second_part_sent.set()


def complete_handshake(sock: ssl.SSLSocket) -> None:
    '''Exchange the connection preface and SETTINGS frames.

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


def print_goaway(payload: bytes, when: str, start: float) -> Tuple[int, int]:
    '''Print a GOAWAY frame and return its last stream id and error code.

    :param payload: The GOAWAY frame payload.
    :param when: Whether the frame came before or after the connection error.
    :param start: The monotonic time at which the request was sent.
    :return: The last stream id and the error code.
    '''
    last_stream_id = int.from_bytes(payload[0:4], 'big') & 0x7FFFFFFF
    error_code = int.from_bytes(payload[4:8], 'big')
    print(f'{when}: GOAWAY error_code={error_code} last_stream_id={last_stream_id} elapsed={time.monotonic() - start:.2f}')
    return last_stream_id, error_code


def read_until_half_closed(sock: ssl.SSLSocket, start: float) -> bytes:
    '''Read frames until ATS sends the GOAWAY that half-closes the session.

    A graceful shutdown sends a first GOAWAY with last stream id 2^31-1, then
    after about two seconds a second one with the last stream id it accepted,
    and from then on the session is half-closed.

    :param sock: The socket to read from.
    :param start: The monotonic time at which the request was sent.
    :return: The DATA payload received so far on the stream.
    '''
    body = b''
    while True:
        frame_type, _, stream_id, payload = read_frame(sock)
        if frame_type == FRAME_TYPE_GOAWAY:
            last_stream_id, error_code = print_goaway(payload, 'before error', start)
            if last_stream_id == STREAM_ID and error_code == 0:
                return body
        elif frame_type == FRAME_TYPE_DATA and stream_id == STREAM_ID:
            body += payload


def wait_for_queued_data(traffic_out: Path, timeout: float) -> bool:
    '''Wait until ATS logs that it queued the DATA frame for the second part.

    The line is logged just before the frame is queued, under the session
    lock the PING handler also needs, so the frame is queued before ATS can
    act on the PING.

    :param traffic_out: The ATS traffic.out file.
    :param timeout: How long to wait, in seconds.
    :return: Whether the line appeared in time.
    '''
    expression = re.compile(rf'\[{STREAM_ID}\] Send a DATA frame .* payload: +{len(SECOND_PART)} ')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if traffic_out.exists() and expression.search(traffic_out.read_text(errors='replace')):
            return True
        time.sleep(0.05)
    return False


def read_until_closed(sock: ssl.SSLSocket, start: float) -> bytes:
    '''Read frames until ATS closes the connection.

    :param sock: The socket to read from.
    :param start: The monotonic time at which the request was sent.
    :return: The DATA payload received on the stream.
    '''
    body = b''
    try:
        while True:
            frame_type, flags, stream_id, payload = read_frame(sock)
            if frame_type == FRAME_TYPE_GOAWAY:
                print_goaway(payload, 'after error', start)
            elif frame_type == FRAME_TYPE_DATA and stream_id == STREAM_ID:
                body += payload
                if flags & FLAG_END_STREAM:
                    print('stream 1: END_STREAM')
    except (EOFError, ConnectionResetError, ssl.SSLError) as e:
        print(f'connection closed ({type(e).__name__}) elapsed={time.monotonic() - start:.2f}')
    return body


def run(args: argparse.Namespace) -> int:
    '''Drive the session to a connection error after it is half-closed.

    :param args: The parsed command line arguments.
    :return: The process exit code.
    '''
    origin = Origin(args.origin_port, args.socket_timeout)
    origin.start()

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])
    raw = socket.create_connection(('127.0.0.1', args.ats_port), timeout=args.socket_timeout)
    try:
        with ctx.wrap_socket(raw, server_hostname='localhost') as sock:
            if sock.selected_alpn_protocol() != 'h2':
                print(f'FAIL: failed to negotiate h2, got {sock.selected_alpn_protocol()!r}', file=sys.stderr)
                return 1
            complete_handshake(sock)

            block = hpack.Encoder().encode(
                [
                    (':method', 'GET'),
                    (':scheme', 'https'),
                    (':authority', 'example.com'),
                    (':path', '/half-closed'),
                ]
            )
            start = time.monotonic()
            sock.sendall(make_frame(FRAME_TYPE_HEADERS, FLAG_END_STREAM | FLAG_END_HEADERS, STREAM_ID, block))

            body = read_until_half_closed(sock, start)

            # ATS queues the DATA frame for the second part without a flush,
            # and the long write_time_threshold keeps it queued.
            origin.send_second_part.set()
            if not origin.second_part_sent.wait(args.socket_timeout) or origin.error is not None:
                print(f'FAIL: the origin did not send the second part: {origin.error}', file=sys.stderr)
                return 1
            if not wait_for_queued_data(args.traffic_out, args.socket_timeout):
                print('FAIL: ATS did not log queuing the DATA frame for the second part', file=sys.stderr)
                return 1

            # A PING on a stream other than 0 is a connection error. The
            # session already sent its last GOAWAY, so it sends no other.
            print(f'sending PING on stream {STREAM_ID} elapsed={time.monotonic() - start:.2f}')
            ping_sent = time.monotonic()
            sock.sendall(make_frame(FRAME_TYPE_PING, 0, STREAM_ID, bytes(8)))

            body += read_until_closed(sock, start)
            close_delay = time.monotonic() - ping_sent
    except socket.timeout:
        print(
            f'FAIL: no progress from ATS within the {args.socket_timeout}s socket timeout; origin error: {origin.error}',
            file=sys.stderr,
        )
        return 1
    except (EOFError, ConnectionResetError, ssl.SSLError) as e:
        print(f'FAIL: the connection ended before the session was half-closed: {type(e).__name__}: {e}', file=sys.stderr)
        return 1
    finally:
        origin.join()

    print(f'body={body!r}')
    # The retransmit timer would flush the DATA too, but only after the long
    # write_time_threshold. A close that soon proves the FINI wrote it.
    if close_delay > args.max_close_delay:
        print(f'FAIL: ATS took {close_delay:.2f}s to close after the PING', file=sys.stderr)
        return 1
    if SECOND_PART not in body:
        print('FAIL: the DATA queued before the connection error was not delivered', file=sys.stderr)
        return 1
    print('second part delivered')
    return 0


def parse_args() -> argparse.Namespace:
    '''Parse the command line arguments.

    :return: The parsed arguments.
    '''
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ats_port', type=int, help='ATS TLS port')
    parser.add_argument('origin_port', type=int, help='port for the origin to listen on')
    parser.add_argument('traffic_out', type=Path, help='ATS traffic.out, to see when ATS queues the second part')
    parser.add_argument(
        '--max-close-delay', type=float, default=2.0, help='fail if ATS takes longer than this to close after the PING'
    )
    parser.add_argument('--socket-timeout', type=float, default=20.0, help='socket timeout in seconds')
    return parser.parse_args()


def main() -> int:
    return run(parse_args())


if __name__ == '__main__':
    raise SystemExit(main())
