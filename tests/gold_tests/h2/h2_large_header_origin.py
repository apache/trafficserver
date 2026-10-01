#!/usr/bin/env python3
"""An HTTP/2 origin that answers with very large response header blocks, and a client
that checks what ATS forwards.

The request path selects the response: /<size>/<status>[/body], where size is the
approximate size in bytes of the response header block as ATS prints it. Without
/body the response has no body, so END_STREAM is set on its HEADERS frame.

Proxy Verifier cannot be used as the origin for these sizes: nghttp2 refuses to send
a header block over 64 KiB by default.
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
import socket
import ssl
import sys
import threading

import h2.config
import h2.connection
import h2.events

REASONS = {200: "OK", 204: "No Content", 302: "Found"}
LARGE_FIELD_SIZE = 60000
PAD_FIELD_SIZE = 400
BODY = b"large-header-origin-body"


def filler(size: int) -> str:
    return ("0123456789abcdefghijklmnopqrstuvwxyz" * (size // 36 + 1))[:size]


def printed_size(status: int, fields: list[tuple[str, str]]) -> int:
    status_line = f"HTTP/1.1 {status} {REASONS[status]}\r\n"
    return len(status_line) + sum(len(n) + len(v) + 4 for n, v in fields) + 2


def response_fields(size: int, status: int, with_body: bool) -> list[tuple[str, str]]:
    """Header fields, without :status, whose printed size is close to size bytes.

    The block is made of fields of LARGE_FIELD_SIZE bytes (under the 64 KB limit on a
    single field) followed by PAD_FIELD_SIZE byte fields, so fields cross many 4 KB
    block boundaries.
    """
    fields = []
    if status == 302:
        fields.append(("location", "https://h2-origin.test/landing"))
    fields.append(("cache-control", "private, no-store"))
    if status != 204:
        fields.append(("content-length", str(len(BODY) if with_body else 0)))

    def remaining() -> int:
        return size - printed_size(status, fields)

    large = 0
    while remaining() > LARGE_FIELD_SIZE + 100:
        large += 1
        fields.append((f"x-large-{large}", filler(LARGE_FIELD_SIZE)))
    pads = 0
    while remaining() > 0:
        pads += 1
        name = f"x-pad-{pads}"
        fields.append((name, filler(max(1, min(PAD_FIELD_SIZE, remaining() - len(name) - 4)))))
    return fields


def parse_path(path: str) -> tuple[int, int, bool]:
    parts = path.strip("/").split("/")
    return int(parts[0]), int(parts[1]), len(parts) > 2 and parts[2] == "body"


def handle(sock: ssl.SSLSocket) -> None:
    conn = h2.connection.H2Connection(config=h2.config.H2Configuration(client_side=False, header_encoding="utf-8"))
    try:
        conn.initiate_connection()
        sock.sendall(conn.data_to_send())
        while True:
            data = sock.recv(65535)
            if not data:
                return
            for event in conn.receive_data(data):
                if isinstance(event, h2.events.RequestReceived):
                    path = dict(event.headers)[":path"]
                    size, status, with_body = parse_path(path)
                    headers = [(":status", str(status))] + response_fields(size, status, with_body)
                    conn.send_headers(event.stream_id, headers, end_stream=not with_body)
                    if with_body:
                        conn.send_data(event.stream_id, BODY, end_stream=True)
            sock.sendall(conn.data_to_send())
    except (ConnectionError, ssl.SSLError):
        return
    finally:
        sock.close()


def serve(args: argparse.Namespace) -> int:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)
    ctx.set_alpn_protocols(["h2"])
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.address, args.port))
    srv.listen(16)
    print(f"large header h2 origin listening on {args.address}:{args.port}", flush=True)
    while True:
        conn, _ = srv.accept()
        try:
            tls = ctx.wrap_socket(conn, server_side=True)
        except (ConnectionError, ssl.SSLError) as e:
            sys.stderr.write(f"tls error: {e}\n")
            conn.close()
            continue
        threading.Thread(target=handle, args=(tls,), daemon=True).start()


def check(args: argparse.Namespace) -> int:
    """Request the path through ATS over HTTP/1.1 and verify the response."""
    size, status, with_body = parse_path(args.path)
    request = f"GET {args.path} HTTP/1.1\r\nHost: {args.host}\r\nConnection: close\r\n\r\n"
    with socket.create_connection((args.address, args.port), timeout=30) as sock:
        sock.sendall(request.encode())
        response = b""
        while chunk := sock.recv(65536):
            response += chunk
    head, _, body = response.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    got_status = int(lines[0].split(" ")[1])
    expected_status = args.expect_status if args.expect_status else status
    print(f"{args.path}: status {got_status}, {len(head) + 4} header bytes, {len(body)} body bytes")
    if got_status != expected_status:
        print(f"FAIL: expected status {expected_status}, got {got_status}")
        return 1
    if args.expect_status:
        print("PASS")
        return 0

    received = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        received.setdefault(name.strip().lower(), []).append(value.strip())
    errors = 0
    expected = response_fields(size, status, with_body)
    for name, value in expected:
        if name == "content-length":
            continue
        if value not in received.get(name, []):
            have = received.get(name, ["<absent>"])[0]
            print(f"FAIL: field {name}: expected {len(value)} bytes, got {have[:40]!r} ({len(have)} bytes)")
            errors += 1
    if with_body and BODY not in body:
        print(f"FAIL: body missing, got {body[:80]!r}")
        errors += 1
    if not with_body and body:
        print(f"FAIL: unexpected {len(body)} byte body: {body[:80]!r}")
        errors += 1
    if errors:
        return 1
    print(f"PASS: all {len(expected)} fields matched")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("serve")
    s.add_argument("address")
    s.add_argument("port", type=int)
    s.add_argument("--cert", required=True)
    s.add_argument("--key", required=True)
    c = sub.add_parser("check")
    c.add_argument("address")
    c.add_argument("port", type=int)
    c.add_argument("path")
    c.add_argument("--host", default="h2-origin.test")
    c.add_argument("--expect-status", type=int, default=0)
    args = p.parse_args()
    return serve(args) if args.command == "serve" else check(args)


if __name__ == "__main__":
    sys.exit(main())
