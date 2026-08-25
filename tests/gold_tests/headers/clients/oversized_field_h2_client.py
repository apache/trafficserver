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
Minimal raw HTTP/2 client for the oversized-header autest.

Why this exists: curl and nghttp both refuse to *send* a header set larger than
~60000 bytes (an nghttp2 client-side guard), so they cannot drive the oversized
HTTP/2 header (name or value length >= 65535) path. This client builds the HPACK
block and frames it itself, emitting CONTINUATION frames when the block exceeds
the 16 KB max frame size, so there is no client-side cap. It connects over TLS
with ALPN h2.

Usage:
    oversized_field_h2_client.py PATH NAME_SIZE VALUE_SIZE [HOST] [PORT] [AUTHORITY]
      NAME_SIZE  > 0  -> add a header whose NAME is that many bytes ("x"*N)
      VALUE_SIZE > 0  -> add a header "x-big" whose VALUE is that many bytes
      both 0          -> plain GET (sanity check)
Defaults: HOST=127.0.0.1 PORT=8543 AUTHORITY=example.com

Prints one line:
    status=<:status or None> rst_error=<code> goaway_error=<code> sent_block_bytes=<n> frames=<n>
- status None + goaway_error=9  => HPACK connection error (GOAWAY COMPRESSION_ERROR)
- status None + rst_error set    => stream reset
- status 200 + origin received it => request was forwarded (bug)

Requires: python3 hpack module.
"""
import socket
import ssl
import struct
import sys
import time

from hpack import Decoder, Encoder

path = sys.argv[1] if len(sys.argv) > 1 else "/raw"
name_size = int(sys.argv[2]) if len(sys.argv) > 2 else 0
value_size = int(sys.argv[3]) if len(sys.argv) > 3 else 0
HOST = sys.argv[4] if len(sys.argv) > 4 else "127.0.0.1"
PORT = int(sys.argv[5]) if len(sys.argv) > 5 else 8543
authority = sys.argv[6] if len(sys.argv) > 6 else "example.com"


def frame(ftype, flags, sid, payload):
    return struct.pack(">I", len(payload))[1:] + bytes([ftype, flags]) + struct.pack(">I", sid) + payload


ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
ctx.set_alpn_protocols(["h2"])
raw = socket.create_connection((HOST, PORT), timeout=15)
s = ctx.wrap_socket(raw, server_hostname=authority)
assert s.selected_alpn_protocol() == "h2", f"ALPN negotiation failed: {s.selected_alpn_protocol()}"

s.sendall(b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")
s.sendall(frame(0x4, 0, 0, b""))  # empty SETTINGS
# Do not ACK here: a SETTINGS ACK acknowledges the peer's SETTINGS, which we have
# not received yet. The read loop below sends the ACK when the server's SETTINGS
# frame arrives.

# Send a uuid header so the Proxy Verifier server can key the transaction on it
# (the replay matches each request by uuid). The path maps directly to the key,
# e.g. /h2-normal -> "h2-normal".
hdrs = [(":method", "GET"), (":scheme", "https"), (":authority", authority), (":path", path), ("uuid", path.lstrip("/"))]
if value_size > 0:
    hdrs.append(("x-big", "A" * value_size))
if name_size > 0:
    hdrs.append(("x" * name_size, "small"))
block = Encoder().encode(hdrs)

MAXF = 16384
chunks = [block[i:i + MAXF] for i in range(0, len(block), MAXF)] or [b""]
flags = 0x1 | (0x4 if len(chunks) == 1 else 0)  # END_STREAM; END_HEADERS only if single frame
s.sendall(frame(0x1, flags, 1, chunks[0]))
for i, c in enumerate(chunks[1:], start=1):
    last = (i == len(chunks) - 1)
    s.sendall(frame(0x9, 0x4 if last else 0, 1, c))  # CONTINUATION, END_HEADERS on last

dec = Decoder()
status = rst = goaway = None
# Generous read deadline: on a loaded CI host (parallel autest shards under ASAN)
# ATS can take several seconds to receive the oversized CONTINUATION-framed
# HEADERS, reject it, and send GOAWAY. A tight timeout turns that latency into a
# spurious "wrong code" failure, so allow ample time and report a distinct
# timed_out marker if no terminal frame (HEADERS/RST/GOAWAY) ever arrives.
READ_DEADLINE_S = 30
timed_out = False
buf = b""
end = time.time() + READ_DEADLINE_S
try:
    while time.time() < end:
        s.settimeout(max(0.1, end - time.time()))
        data = s.recv(65536)
        if not data:
            break
        buf += data
        while len(buf) >= 9:
            ln = struct.unpack(">I", b"\x00" + buf[:3])[0]
            ftype = buf[3]
            fl = buf[4]
            if len(buf) < 9 + ln:
                break
            payload = buf[9:9 + ln]
            buf = buf[9 + ln:]
            if ftype == 0x1:  # HEADERS
                pl = payload
                pad = 0
                if fl & 0x08:  # PADDED: leading pad-length byte, trailing padding
                    pad = pl[0]
                    pl = pl[1:]
                if fl & 0x20:  # PRIORITY: 5-byte stream-dependency + weight prefix
                    pl = pl[5:]
                if pad:
                    pl = pl[:-pad]
                try:
                    for k, v in dec.decode(pl):
                        if k == ":status":
                            status = v
                except Exception as e:
                    status = f"decode_err:{e}"
            elif ftype == 0x3:  # RST_STREAM
                rst = struct.unpack(">I", payload[:4])[0]
            elif ftype == 0x7:  # GOAWAY
                goaway = struct.unpack(">I", payload[4:8])[0]
            elif ftype == 0x4 and not (fl & 0x1):
                s.sendall(frame(0x4, 0x1, 0, b""))  # ack server SETTINGS
        if status is not None or rst is not None or goaway is not None:
            break
    else:
        timed_out = True  # deadline reached without a terminal frame
except socket.timeout:
    timed_out = True
print(
    f"status={status} rst_error={rst} goaway_error={goaway} timed_out={timed_out} sent_block_bytes={len(block)} frames={len(chunks)}"
)
s.close()
