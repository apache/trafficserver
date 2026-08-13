"""NOOP-path check: DELETE an uncached path (Max-Forwards:0) with a smuggled body.
A cache miss self-answers via INTERNAL_CACHE_NOOP (404); the body must be drained."""

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
smug = f"GET /misspoison HTTP/1.1\r\nHost: {host}\r\n\r\n".encode()
req = (f"DELETE /coldpath HTTP/1.1\r\nHost: {host}\r\nMax-Forwards: 0\r\n"
       f"Content-Length: {len(smug)}\r\n\r\n").encode() + smug
s = socket.create_connection((addr, port), timeout=8)
s.sendall(req)
s.settimeout(3)
data = b""
try:
    while True:
        c = s.recv(65536)
        if not c:
            break
        data += c
except socket.timeout:
    pass
s.close()
st = data.split(b"\r\n", 1)[0].split(b" ")
print(f"DELETE_STATUS={st[1].decode() if len(st)>1 else '?'}")
n = data.count(b"HTTP/1.1 ")
print(f"RESPONSE_COUNT={n}")
print(f"SECOND_RESPONSE_RECEIVED={n>=2}")
sys.stdout.write(data.decode('latin1', 'replace'))
print("\n---end---")
