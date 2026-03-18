#!/usr/bin/env python3
'''
HTTP/2 client that verifies DATA frames for in-flight POST requests are
processed after ATS sends a GOAWAY frame.

stream_requests and stream_error_count in the scenario below refer to ATS
internal counters in Http2ConnectionState.

Scenario:
1. Send 4 GET requests (stream_requests = 4).
2. Send 3 raw DATA frames to the already-closed GET streams. ATS replies with
   RST_STREAM(STREAM_CLOSED) for each, incrementing stream_error_count to 3.
3. Send POST HEADERS only on a new stream, withhold the DATA body
   (stream_requests = 5). With 5 total requests and 3 errors:
     error_rate = 3/5 = 0.6 > min(1.0, 0.2 * 2.0) = 0.4  -> GOAWAY fires.
4. Wait for GOAWAY.
5. Send the POST DATA body *after* GOAWAY.
6. Verify that ATS returns 200 OK (not 408 Request Timeout).
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
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
#  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
#  either express or implied.  See the License for the specific
#  language governing permissions and limitations under the
#  License.

from __future__ import annotations

import argparse
import socket
import ssl
import struct

import h2.config
import h2.connection
import h2.events
import h2.exceptions


def get_socket(port: int) -> ssl.SSLSocket:
    """Return a TLS socket connected to localhost:port with h2 negotiated."""
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(['h2'])
    socket.setdefaulttimeout(5)
    raw = socket.create_connection(('localhost', port))
    return ctx.wrap_socket(raw, server_hostname='localhost')


def send_raw_data_frame(sock: ssl.SSLSocket, stream_id: int, payload: bytes = b'\x00', end_stream: bool = False) -> None:
    """Send a raw HTTP/2 DATA frame, bypassing the h2 library.

    Used to send DATA to already-closed streams so that ATS replies with
    RST_STREAM(STREAM_CLOSED) and increments its stream_error_count.
    """
    flags = 0x01 if end_stream else 0x00
    length = len(payload)
    # Frame header: 3-byte length | 1-byte type | 1-byte flags | 4-byte stream id
    header = struct.pack('>I', length)[1:]  # 3-byte big-endian length
    header += b'\x00'  # type: DATA = 0x0
    header += bytes([flags])
    header += struct.pack('>I', stream_id & 0x7FFFFFFF)
    sock.sendall(header + payload)


def drain_events(sock: ssl.SSLSocket, h2conn: h2.connection.H2Connection) -> list:
    """Read one chunk from the socket and return the resulting h2 events."""
    data = sock.recv(65536)
    if not data:
        return []
    try:
        events = h2conn.receive_data(data)
    except h2.exceptions.ProtocolError:
        events = []
    pending = h2conn.data_to_send()
    if pending:
        sock.sendall(pending)
    return events


def _recv_exact(sock: ssl.SSLSocket, n: int) -> bytes | None:
    """Read exactly n bytes from the socket, or return None on EOF."""
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def recv_response_after_goaway(sock: ssl.SSLSocket, h2conn: h2.connection.H2Connection, stream_id: int) -> str | None:
    """Receive the HTTP/2 response for stream_id by parsing raw frames.

    The h2 library raises ProtocolError when receive_data() is called after
    GOAWAY has been received, even though RFC 9113 §6.8 allows the peer to
    keep sending responses for streams with IDs <= last_stream_id.  This
    function bypasses the h2 state machine and reads frames at the wire level,
    using h2's built-in HPACK decoder to keep header-compression state in sync.

    :returns: The value of the :status pseudo-header, or None on failure.
    """
    status = None
    stream_ended = False
    frame_header_len = 9

    while not stream_ended:
        raw_hdr = _recv_exact(sock, frame_header_len)
        if raw_hdr is None:
            break

        payload_len = struct.unpack('>I', b'\x00' + raw_hdr[:3])[0]
        frame_type = raw_hdr[3]
        flags = raw_hdr[4]
        frame_sid = struct.unpack('>I', raw_hdr[5:9])[0] & 0x7FFFFFFF

        payload = _recv_exact(sock, payload_len) or b''

        if frame_sid != stream_id:
            continue

        if frame_type == 0x1:  # HEADERS
            # Strip optional PADDED (0x08) and PRIORITY (0x20) bytes.
            block = payload
            if flags & 0x08:
                pad_len = block[0]
                block = block[1:len(block) - pad_len]
            if flags & 0x20:
                block = block[5:]
            # Use h2's HPACK decoder so its dynamic table stays in sync.
            # decoder.decode() may return (bytes, bytes) or (str, str)
            # depending on the hpack library version, so normalise to str.
            for name, value in h2conn.decoder.decode(block):
                name_str = name.decode() if isinstance(name, bytes) else name
                value_str = value.decode() if isinstance(value, bytes) else value
                if name_str == ':status':
                    status = value_str
            if flags & 0x01:  # END_STREAM
                stream_ended = True
        elif frame_type == 0x0:  # DATA
            if flags & 0x01:  # END_STREAM
                stream_ended = True

    return status


def run(port: int, path: str) -> None:
    tls_socket = get_socket(port)

    config = h2.config.H2Configuration(client_side=True)
    h2conn = h2.connection.H2Connection(config=config)
    h2conn.initiate_connection()
    tls_socket.sendall(h2conn.data_to_send())

    get_headers = [
        (':method', 'GET'),
        (':path', path),
        (':authority', 'localhost'),
        (':scheme', 'https'),
    ]

    # Step 1: Send 4 GET requests on streams 1, 3, 5, 7 (stream_requests = 4).
    # stream_error_rate_threshold=0.2 requires total >= 1/0.2 = 5 before the
    # rate is calculated, so we need at least one more request after these.
    for stream_id in [1, 3, 5, 7]:
        h2conn.send_headers(stream_id, get_headers, end_stream=True)
    tls_socket.sendall(h2conn.data_to_send())

    # Wait for all 4 GET responses to complete.
    completed: set = set()
    while len(completed) < 4:
        for event in drain_events(tls_socket, h2conn):
            if isinstance(event, h2.events.StreamEnded):
                completed.add(event.stream_id)

    # Step 2: Send raw DATA frames to the already-closed streams 1, 3, and 5.
    # ATS responds with RST_STREAM(STREAM_CLOSED) for each, incrementing
    # stream_error_count to 3.
    for closed_stream_id in [1, 3, 5]:
        send_raw_data_frame(tls_socket, closed_stream_id)

    # Step 3: Start a POST on stream 9 – send HEADERS only, withhold DATA
    # (stream_requests = 5). The body will be sent after GOAWAY.
    post_headers = [
        (':method', 'POST'),
        (':path', path),
        (':authority', 'localhost'),
        (':scheme', 'https'),
        ('content-length', '4'),
    ]
    last_stream_id = 9
    h2conn.send_headers(last_stream_id, post_headers, end_stream=False)
    tls_socket.sendall(h2conn.data_to_send())

    # Step 4: Wait for GOAWAY with last_stream_id covering the POST stream.
    goaway_received = False
    goaway_error_code = None
    try:
        while not goaway_received:
            for event in drain_events(tls_socket, h2conn):
                if isinstance(event, h2.events.ConnectionTerminated) and event.last_stream_id == last_stream_id:
                    goaway_received = True
                    goaway_error_code = event.error_code
    except socket.timeout:
        print(f"ERROR: Timed out waiting for GOAWAY with last_stream_id={last_stream_id}")
        exit(1)

    print(f"GOAWAY received with error_code={goaway_error_code}")

    # Step 5: Send the POST DATA body after GOAWAY.  Before the fix, ATS would
    # stop reading frames after GOAWAY, causing this DATA frame to be ignored
    # and the request to time out with 408.
    body = b'body'
    try:
        h2conn.send_data(last_stream_id, body, end_stream=True)
        pending = h2conn.data_to_send()
        if pending:
            tls_socket.sendall(pending)
    except h2.exceptions.ProtocolError:
        # The h2 library rejects sends after GOAWAY; fall back to raw bytes.
        send_raw_data_frame(tls_socket, last_stream_id, body, end_stream=True)

    # Step 6: Receive the POST response on the POST stream by parsing raw frames.
    # h2's receive_data() raises ProtocolError after GOAWAY, so we bypass the
    # h2 state machine and decode headers with h2's built-in HPACK decoder.
    response_status = recv_response_after_goaway(tls_socket, h2conn, last_stream_id)

    if response_status == '200':
        print("SUCCESS: POST request completed with 200 OK after GOAWAY")
    else:
        print(f"ERROR: Expected 200 OK, got status={response_status}")
        exit(1)

    tls_socket.close()


def main():
    parser = argparse.ArgumentParser(description='Test in-flight POST DATA handling after GOAWAY.')
    parser.add_argument('port', type=int, help='ATS TLS port')
    parser.add_argument('path', help='Request path (e.g. /test)')
    args = parser.parse_args()
    run(args.port, args.path)


if __name__ == '__main__':
    main()
