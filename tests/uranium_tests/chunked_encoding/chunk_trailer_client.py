#!/usr/bin/env python3
'''
Send a single chunked POST whose body ends with a bare-LF trailer terminator. Per
RFC 9112 Section 7.1 the trailer section ends with an empty line, "CRLF". A parser
that accepts a bare LF blank line ends the body one byte early, so the bytes that
follow are framed as a separate, smuggled request. A parser that requires CRLF
rejects the request instead and never forwards the embedded GET.

The script prints the number of HTTP responses received on the connection and the
raw response, so the test can assert that exactly one response came back and that
the embedded endpoint was not reached.
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

import socket
import sys
import time

# A chunked POST whose final (zero-size) chunk is followed by a bare LF instead of
# CRLF as the trailer terminator. A correct parser requires CRLF and rejects the
# request. A parser that accepts the bare LF ends the body at "0\r\n\n", reads the
# trailing GET as a separate pipelined request, and forwards it to the origin. The
# origin answers that GET /smuggled with a distinctive body, so if it ever reaches
# the client the request was smuggled. The body is empty (the bug is in the
# trailer terminator, not the body) so the origin drains it cleanly.
PAYLOAD = (
    b"POST /legit HTTP/1.1\r\n"
    b"Host: localhost\r\n"
    b"uuid: 1\r\n"
    b"Transfer-Encoding: chunked\r\n"
    b"\r\n"
    b"0\r\n"
    b"\n"
    b"GET /smuggled HTTP/1.1\r\n"
    b"Host: localhost\r\n"
    b"uuid: 2\r\n"
    b"Connection: close\r\n"
    b"\r\n")

# Byte offset at which to split the payload across two sends, right after the
# final "0\r\n" and before the bare LF terminator, so the proxy must suspend
# parsing the trailer on the first read and resume it (and reject) on the second.
SPLIT_AT = PAYLOAD.index(b"0\r\n") + len(b"0\r\n")


def main() -> int:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <host> <port> [--split]", file=sys.stderr)
        return 2

    host, port = sys.argv[1], int(sys.argv[2])
    split = "--split" in sys.argv[3:]

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    response = b""
    try:
        sock.connect((host, port))
        if split:
            sock.sendall(PAYLOAD[:SPLIT_AT])
            time.sleep(0.3)  # Force the remainder into a separate read on the proxy.
            try:
                sock.sendall(PAYLOAD[SPLIT_AT:])
            except OSError:
                pass  # The proxy may have already rejected and closed the connection.
        else:
            sock.sendall(PAYLOAD)
        while True:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            response += chunk
    finally:
        sock.close()

    responses = response.count(b"HTTP/1.1 ")
    status = response.split(b"\r\n", 1)[0].decode(errors="replace") if response else "(no response)"
    print(f"responses={responses}")
    print(f"status={status}")
    print("=== response ===")
    print(response.decode(errors="replace"))
    print("=== end ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
