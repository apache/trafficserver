#!/usr/bin/env python3

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
"""
Send an HTTP/2 POST whose Content-Length may not describe the DATA payload.

The declared Content-Length is chosen per scenario so that a proxy which
truncates it to a narrower integer type, or which miscounts the padding of a
PADDED DATA frame, sees a value that appears consistent with the payload
actually sent.
"""

import argparse
import socket
import ssl
import sys

import hpack

H2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

TYPE_DATA = 0x00
TYPE_HEADERS = 0x01
TYPE_RST_STREAM = 0x03
TYPE_SETTINGS = 0x04
TYPE_GOAWAY = 0x07

FLAG_ACK = 0x01
FLAG_END_STREAM = 0x01
FLAG_END_HEADERS = 0x04
FLAG_PADDED = 0x08

NO_ERROR = 0x00
PROTOCOL_ERROR = 0x01

BODY = b"abc"

# ATS may reject an inconsistent Content-Length either at the HTTP/2 layer, with
# a PROTOCOL_ERROR, or at the HTTP layer, with a 4xx response.
EXPECT_REJECTED = "rejected"
EXPECT_PROXIED = "proxied"

# Each scenario declares a Content-Length that disagrees with the DATA actually
# sent, except for the '*-matches' scenarios, which are valid control cases.
#
# 4294967296 is 2**32, which truncates to a uint32_t 0. 4294967299 is 2**32 + 3,
# which truncates to a uint32_t 3 and so matches the payload length.
#
# The 'padded-*' rejection scenarios send a PADDED DATA frame too short to hold
# both the Pad Length field and the padding it declares, which RFC 9113 section
# 6.1 requires be treated as a PROTOCOL_ERROR. A proxy that omits the Pad Length
# field from its padding check computes a data length of
# payload_length - pad_length - 1 for such a frame: 4 - 4 - 1 for a Pad Length
# equal to the frame length, and 0 - 0 - 1 for an empty frame. Either wraps to
# 4294967295 as a uint32_t, so a Content-Length of 4294967295, or 4294967298
# when 3 more bytes of DATA follow, appears consistent with it.
#
# Each value is (Content-Length, DATA frames as (flags, payload), expectation).
PAD_FILLS_FRAME = bytes([len(BODY) + 1]) + BODY
PAD_LEAVES_NO_DATA = bytes([len(BODY)]) + bytes(len(BODY))
SCENARIOS = {
    "cl-wraps-to-zero": ("4294967296", ((FLAG_END_STREAM, BODY),), EXPECT_REJECTED),
    "cl-wraps-to-payload-length": ("4294967299", ((FLAG_END_STREAM, BODY),), EXPECT_REJECTED),
    "cl-larger-than-payload": ("10", ((FLAG_END_STREAM, BODY),), EXPECT_REJECTED),
    "cl-negative": ("-3", ((FLAG_END_STREAM, BODY),), EXPECT_REJECTED),
    "cl-matches": (str(len(BODY)), ((FLAG_END_STREAM, BODY),), EXPECT_PROXIED),
    "padded-pad-length-equals-frame-length": ("4294967295", ((FLAG_END_STREAM | FLAG_PADDED, PAD_FILLS_FRAME),), EXPECT_REJECTED),
    "padded-pad-length-equals-frame-length-then-body":
        ("4294967298", ((FLAG_PADDED, PAD_FILLS_FRAME), (FLAG_END_STREAM, BODY)), EXPECT_REJECTED),
    "padded-empty-frame": ("4294967295", ((FLAG_END_STREAM | FLAG_PADDED, b""),), EXPECT_REJECTED),
    "padded-matches": (str(len(BODY)), ((FLAG_END_STREAM | FLAG_PADDED, bytes([2]) + BODY + bytes(2)),), EXPECT_PROXIED),
    "padded-no-data-then-body-matches":
        (str(len(BODY)), ((FLAG_PADDED, PAD_LEAVES_NO_DATA), (FLAG_END_STREAM, BODY)), EXPECT_PROXIED),
}


def make_frame(frame_type: int, flags: int, stream_id: int, payload: bytes = b"") -> bytes:
    return (len(payload).to_bytes(3, "big") + bytes([frame_type, flags]) + (stream_id & 0x7FFFFFFF).to_bytes(4, "big") + payload)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: socket.socket):
    header = recv_exact(sock, 9)
    if len(header) == 0:
        return None
    if len(header) != 9:
        raise RuntimeError(f"incomplete frame header: got {len(header)} bytes")

    length = int.from_bytes(header[0:3], "big")
    payload = recv_exact(sock, length)
    if len(payload) != length:
        raise RuntimeError(f"incomplete frame payload: expected {length}, got {len(payload)}")

    return {
        "length": length,
        "type": header[3],
        "flags": header[4],
        "stream_id": int.from_bytes(header[5:9], "big") & 0x7FFFFFFF,
        "payload": payload,
    }


def connect_socket(port: int) -> socket.socket:
    socket.setdefaulttimeout(5)

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(["h2"])

    tls_socket = socket.create_connection(("127.0.0.1", port))
    tls_socket = ctx.wrap_socket(tls_socket, server_hostname="localhost")
    if tls_socket.selected_alpn_protocol() != "h2":
        raise RuntimeError(f"failed to negotiate h2, got {tls_socket.selected_alpn_protocol()!r}")
    return tls_socket


def make_headers(scenario: str) -> bytes:
    encoder = hpack.Encoder()
    headers = [
        (":method", "POST"),
        (":scheme", "https"),
        (":authority", "www.example.com"),
        (":path", f"/{scenario}"),
        ("content-length", SCENARIOS[scenario][0]),
        ("uuid", scenario),
    ]

    return encoder.encode(headers)


def send_request(tls_socket: socket.socket, scenario: str) -> None:
    """Finish SETTINGS before sending a potentially connection-fatal request.

    :param tls_socket: Connected TLS socket with HTTP/2 negotiated.
    :param scenario: Key in SCENARIOS describing the request frames.
    """

    tls_socket.sendall(H2_PREFACE + make_frame(TYPE_SETTINGS, 0, 0))
    received_settings = False
    received_ack = False
    while not (received_settings and received_ack):
        frame = read_frame(tls_socket)
        if frame is None or frame["type"] == TYPE_GOAWAY:
            raise RuntimeError("Connection closed during the SETTINGS handshake")
        if frame["type"] == TYPE_SETTINGS:
            if frame["flags"] & FLAG_ACK:
                received_ack = True
            else:
                tls_socket.sendall(make_frame(TYPE_SETTINGS, FLAG_ACK, 0))
                received_settings = True

    # A separate TLS write after malformed DATA can race ATS closing the
    # connection. Submit all request frames together, then only read the reply.
    request = make_frame(TYPE_HEADERS, FLAG_END_HEADERS, 1, make_headers(scenario))
    for data_flags, data_payload in SCENARIOS[scenario][1]:
        request += make_frame(TYPE_DATA, data_flags, 1, data_payload)
    tls_socket.sendall(request)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=int, help="TLS port to connect to")
    parser.add_argument("scenario", choices=sorted(SCENARIOS), help="Content-Length shape to send")
    args = parser.parse_args()

    tls_socket = connect_socket(args.port)
    decoder = hpack.Decoder()
    _, _, expectation = SCENARIOS[args.scenario]
    rejection_expected = expectation == EXPECT_REJECTED
    try:
        send_request(tls_socket, args.scenario)

        while True:
            frame = read_frame(tls_socket)
            if frame is None:
                print(f"Connection closed without a response for scenario {args.scenario}", file=sys.stderr)
                return 1

            frame_type = frame["type"]
            if frame_type == TYPE_SETTINGS and not (frame["flags"] & FLAG_ACK):
                tls_socket.sendall(make_frame(TYPE_SETTINGS, FLAG_ACK, 0))
                continue

            if frame_type == TYPE_RST_STREAM and frame["stream_id"] == 1:
                error_code = int.from_bytes(frame["payload"][0:4], "big")
                print(f"Received RST_STREAM on stream 1 with error code {error_code}")
                return 0 if rejection_expected and error_code == PROTOCOL_ERROR else 1

            if frame_type == TYPE_GOAWAY:
                error_code = int.from_bytes(frame["payload"][4:8], "big")
                print(f"Received GOAWAY with error code {error_code}")
                return 0 if rejection_expected and error_code in (NO_ERROR, PROTOCOL_ERROR) else 1

            if frame_type == TYPE_HEADERS and frame["stream_id"] == 1:
                status = dict(decoder.decode(frame["payload"])).get(":status", "")
                status_code = int(status) if status else 0
                print(f"Received HTTP/2 response with status {status_code}")
                if rejection_expected:
                    return 0 if 400 <= status_code < 500 else 1
                return 0 if status_code == 200 else 1
    except socket.timeout:
        print(f"Timed out waiting for a response for scenario {args.scenario}", file=sys.stderr)
        return 1
    except (ConnectionError, ssl.SSLError) as e:
        print(f"Connection failed before a response for scenario {args.scenario}: {e}", file=sys.stderr)
        return 1
    finally:
        tls_socket.close()


if __name__ == "__main__":
    raise SystemExit(main())
