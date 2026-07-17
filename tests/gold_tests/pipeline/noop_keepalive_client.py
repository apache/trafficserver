"""Double-drain / non-idempotency audit for the NOOP self-response path.
On one keep-alive connection: (1) a DELETE to an uncached path (Max-Forwards:0) with a
benign fully-buffered body self-answers 404 via INTERNAL_CACHE_NOOP and must drain the
body EXACTLY once; (2) a following GET / must still be served. If the NOOP path drained
twice, do_drain_request_body (not idempotent) would stamp Connection: close and the GET
would get nothing."""

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

addr, port, host = sys.argv[1], int(sys.argv[2]), sys.argv[3]


def recv_one(s: socket.socket) -> bytes:
    s.settimeout(4)
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            c = s.recv(65536)
            if not c:
                break
            buf += c
    except socket.timeout:
        pass
    return buf


body = b"xxxxx"
r1 = (f"DELETE /coldpath HTTP/1.1\r\nHost: {host}\r\nMax-Forwards: 0\r\n"
      f"Content-Length: {len(body)}\r\n\r\n").encode() + body
r2 = (f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n").encode()
s = socket.create_connection((addr, port), timeout=8)
s.sendall(r1)
resp1 = recv_one(s)
st1 = (resp1.split(b" ") + [b"?"])[1].decode('latin1', 'replace') if resp1 else "NONE"
s.sendall(r2)
resp2 = recv_one(s)
st2 = (resp2.split(b" ") + [b"?"])[1].decode('latin1', 'replace') if resp2 else "NONE"
print(f"DELETE_STATUS={st1}")
print(f"SECOND_REQUEST_STATUS={st2}")
print(f"KEEPALIVE_PRESERVED={'yes' if resp2 else 'no'}")
s.close()
