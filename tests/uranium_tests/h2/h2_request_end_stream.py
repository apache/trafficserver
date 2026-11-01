#!/usr/bin/env python3
"""An HTTP/2 origin that answers a request only once its stream has ended, and
clients: an HTTP/2 one that places its END_STREAM in different frames, and an
HTTP/1.1 one that frames its body with Content-Length or chunked encoding.

The origin replies when it sees END_STREAM, reporting the method, the number of
body bytes it received and whether END_STREAM came on HEADERS or on DATA.  A
request whose END_STREAM ATS never forwards gets no reply from the origin, so ATS
gives up with a 504 once its origin no-activity timeout fires.

The origin delays each TLS handshake so that, for a request whose client sends the
DATA frame carrying END_STREAM in the same write as HEADERS, the client stream has
ended before ATS can start the request body tunnel.  That ordering is otherwise
left to a race.
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
import random
import socket
import ssl
import sys
import threading
import time

import h2.config
import h2.connection
import h2.events
import h2.exceptions

# How the client ends the request stream:
#   headers  END_STREAM on HEADERS.
#   data     HEADERS, then one DATA frame (empty or carrying the body) with END_STREAM, in one write.
#   late     HEADERS, then a pause well past the origin handshake delay, then the DATA frame.
ENDINGS = ("headers", "data", "late")

# The frame that carried END_STREAM to the origin.
END_FRAMES = ("headers", "data")


def filler(size: int) -> str:
    """Return a random header value of the given size, which HPACK's Huffman coding shrinks only slightly."""
    return "".join(random.Random(size).choices("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", k=size))


def serve(args: argparse.Namespace) -> int:
    """Accept origin connections from ATS, serving each on its own thread."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)
    ctx.set_alpn_protocols(["h2"])
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.address, args.port))
    srv.listen(16)
    print(f"end stream origin listening on {args.address}:{args.port}", flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=handle, args=(ctx, conn, args.handshake_delay), daemon=True).start()


def handle(ctx: ssl.SSLContext, conn: socket.socket, delay: float) -> None:
    """Serve one origin connection, answering each request only after its stream ends."""
    # A port probe connects and closes without a handshake; delaying it is harmless.
    time.sleep(delay)
    try:
        sock = ctx.wrap_socket(conn, server_side=True)
    except (ConnectionError, ssl.SSLError, OSError):
        conn.close()
        return
    h2c = h2.connection.H2Connection(config=h2.config.H2Configuration(client_side=False, header_encoding="utf-8"))
    requests: dict[int, dict] = {}
    try:
        h2c.initiate_connection()
        sock.sendall(h2c.data_to_send())
        while True:
            data = sock.recv(65535)
            if not data:
                return
            for event in h2c.receive_data(data):
                if isinstance(event, h2.events.RequestReceived):
                    requests[event.stream_id] = {"headers": dict(event.headers), "bytes": 0, "end": "data"}
                    if event.stream_ended is not None:
                        requests[event.stream_id]["end"] = "headers"
                elif isinstance(event, h2.events.DataReceived):
                    requests[event.stream_id]["bytes"] += len(event.data)
                    h2c.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                elif isinstance(event, h2.events.StreamEnded):
                    request = requests.pop(event.stream_id)
                    method = request["headers"][":method"]
                    print(
                        f"origin: {method} {request['headers'][':path']} ended on {request['end']} "
                        f"with {request['bytes']} body bytes",
                        flush=True)
                    body = b"ended"
                    h2c.send_headers(
                        event.stream_id, [
                            (":status", "200"),
                            ("content-length", str(len(body))),
                            ("x-origin-method", method),
                            ("x-origin-body-bytes", str(request["bytes"])),
                            ("x-origin-end-frame", request["end"]),
                        ])
                    h2c.send_data(event.stream_id, body, end_stream=True)
            sock.sendall(h2c.data_to_send())
    except h2.exceptions.ProtocolError as e:
        # A frame ATS should not have sent, such as DATA after END_STREAM.
        print(f"origin: protocol error from ATS: {e!r}", flush=True)
    except (ConnectionError, ssl.SSLError, OSError):
        return
    finally:
        for stream_id, request in requests.items():
            print(f"origin: stream {stream_id} {request['headers'][':method']} never ended", flush=True)
        sock.close()


def check_origin_reply(
        args: argparse.Namespace, status: int | None, fields: dict[str, str], response_body: bytes, forwarded: int) -> int:
    """Check the reply the origin sent once it saw the request end, returning the exit status."""
    if status != 200:
        print(f"FAIL: expected status 200, got {status}")
        return 1
    if fields.get("x-origin-method") != args.method:
        print(f"FAIL: origin did not answer this request: {fields}")
        return 1
    if fields.get("x-origin-body-bytes") != str(forwarded):
        print(f"FAIL: origin received {fields.get('x-origin-body-bytes')} body bytes, expected {forwarded}")
        return 1
    if args.origin_end_frame and fields.get("x-origin-end-frame") != args.origin_end_frame:
        print(f"FAIL: END_STREAM reached the origin on {fields.get('x-origin-end-frame')}, expected {args.origin_end_frame}")
        return 1
    if response_body != b"ended":
        print(f"FAIL: unexpected response body {response_body[:80]!r}")
        return 1
    print("PASS")
    return 0


def request(args: argparse.Namespace) -> int:
    """Send one request to ATS over HTTP/2 and check the origin saw it end."""
    body = args.body.encode()
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_alpn_protocols(["h2"])
    start = time.monotonic()
    with socket.create_connection((args.address, args.port), timeout=args.timeout) as raw:
        sock = ctx.wrap_socket(raw, server_hostname="localhost")
        h2c = h2.connection.H2Connection(config=h2.config.H2Configuration(client_side=True, header_encoding="utf-8"))
        h2c.initiate_connection()
        headers = [(":method", args.method), (":scheme", "https"), (":authority", "end-stream.test"), (":path", args.path)]
        if args.content_length is not None:
            headers.append(("content-length", str(args.content_length)))
        if args.header_size:
            headers.append(("x-filler", filler(args.header_size)))
        stream_id = h2c.get_next_available_stream_id()
        h2c.send_headers(stream_id, headers, end_stream=args.end == "headers")
        if args.end == "late":
            sock.sendall(h2c.data_to_send())
            time.sleep(args.late_delay)
        if args.end != "headers":
            h2c.send_data(stream_id, body, end_stream=True)
        sock.sendall(h2c.data_to_send())

        status = None
        response_headers: dict[str, str] = {}
        response_body = b""
        done = False
        while not done:
            try:
                data = sock.recv(65535)
            except socket.timeout:
                print(f"FAIL: no complete response after {time.monotonic() - start:.1f}s")
                return 1
            if not data:
                break
            for event in h2c.receive_data(data):
                if isinstance(event, h2.events.ResponseReceived):
                    response_headers = dict(event.headers)
                    status = int(response_headers[":status"])
                elif isinstance(event, h2.events.DataReceived):
                    response_body += event.data
                    h2c.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                elif isinstance(event, (h2.events.StreamEnded, h2.events.StreamReset, h2.events.ConnectionTerminated)):
                    done = True
            sock.sendall(h2c.data_to_send())

    print(
        f"{args.method} end={args.end} content-length={args.content_length} body={len(body)}: "
        f"status {status} in {time.monotonic() - start:.1f}s, origin saw {response_headers.get('x-origin-body-bytes')} "
        f"body bytes and END_STREAM on {response_headers.get('x-origin-end-frame')}")
    forwarded = len(body) if args.origin_body_bytes is None else args.origin_body_bytes
    return check_origin_reply(args, status, response_headers, response_body, forwarded)


def request_h1(args: argparse.Namespace) -> int:
    """Send one request to ATS over HTTP/1.1 and check the origin saw all of its body."""
    body = args.body.encode()
    head = f"{args.method} {args.path} HTTP/1.1\r\nHost: end-stream.test\r\nConnection: close\r\n"
    if args.header_size:
        head += f"X-Filler: {filler(args.header_size)}\r\n"
    if args.framing == "chunked":
        head += "Transfer-Encoding: chunked\r\n"
        first = head.encode() + b"\r\n" + (f"{len(body):x}\r\n".encode() + body + b"\r\n" if body else b"")
        rest = b"0\r\n\r\n"
    else:
        head += f"Content-Length: {len(body)}\r\n"
        first = head.encode() + b"\r\n"
        rest = body
    start = time.monotonic()
    response = b""
    with socket.create_connection((args.address, args.port), timeout=args.timeout) as sock:
        if args.late:
            # The end of the body arrives well after ATS has started forwarding the request.
            sock.sendall(first)
            time.sleep(args.late_delay)
            sock.sendall(rest)
        else:
            sock.sendall(first + rest)
        try:
            while chunk := sock.recv(65536):
                response += chunk
        except socket.timeout:
            print(f"FAIL: no complete response after {time.monotonic() - start:.1f}s")
            return 1
    head_bytes, _, response_body = response.partition(b"\r\n\r\n")
    lines = head_bytes.decode("latin-1").split("\r\n")
    status = int(lines[0].split(" ")[1]) if lines[0] else None
    fields = {name.strip().lower(): value.strip() for name, _, value in (line.partition(":") for line in lines[1:])}
    print(
        f"HTTP/1.1 {args.method} {args.framing} body={len(body)}: status {status} in {time.monotonic() - start:.1f}s, "
        f"origin saw {fields.get('x-origin-body-bytes')} body bytes and END_STREAM on {fields.get('x-origin-end-frame')}")
    return check_origin_reply(args, status, fields, response_body, len(body))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("serve")
    s.add_argument("address")
    s.add_argument("port", type=int)
    s.add_argument("--cert", required=True)
    s.add_argument("--key", required=True)
    s.add_argument("--handshake-delay", type=float, default=0.5)
    c = sub.add_parser("request")
    c.add_argument("address")
    c.add_argument("port", type=int)
    c.add_argument("method")
    c.add_argument("--end", choices=ENDINGS, required=True)
    c.add_argument("--content-length", type=int)
    c.add_argument("--body", default="")
    c.add_argument("--late-delay", type=float, default=1.5)
    c.add_argument("--origin-body-bytes", type=int, help="body bytes the origin should receive, if not the whole body")
    h = sub.add_parser("request-h1")
    h.add_argument("address")
    h.add_argument("port", type=int)
    h.add_argument("method")
    h.add_argument("--framing", choices=("length", "chunked"), required=True)
    h.add_argument("--body", default="")
    h.add_argument("--late", action="store_true", help="send the end of the body after a pause")
    h.add_argument("--late-delay", type=float, default=1.5)
    for client in (c, h):
        client.add_argument("--path", default="/end-stream")
        client.add_argument("--timeout", type=float, default=15)
        client.add_argument("--header-size", type=int, default=0, help="size of an extra request header value")
        client.add_argument("--origin-end-frame", choices=END_FRAMES, help="frame that should carry END_STREAM to the origin")
    args = p.parse_args()
    if args.command == "serve":
        return serve(args)
    return request(args) if args.command == "request" else request_h1(args)


if __name__ == "__main__":
    sys.exit(main())
