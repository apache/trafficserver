#!/usr/bin/env python3
"""An HTTP/2 (TLS) origin that sends a 1xx interim response before the final 200.

Proxy Verifier cannot emit interim/1xx responses, so this hand-frames HTTP/2 so we
can exercise ATS origin-side handling of 1xx interim responses.

Modes (chosen by --mode):
  single   : 103 Early Hints, then 200            (the deepwiki/Vercel case)
  multi    : 103, 103, 100, then 200              (multiple sequential interims)
  continue : 100 Continue, then 200
  cont     : a single 103 whose header block is split across HEADERS+CONTINUATION,
             then 200                              (multi-frame interim)
  none     : 200 only                             (control; must always pass)
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
import struct
import subprocess
import sys
import tempfile
import threading

BODY = b"interim-origin-body"


def frame(ftype, flags, sid, payload):
    return struct.pack(">I", len(payload))[1:] + bytes([ftype, flags]) + struct.pack(">I", sid) + payload


def lit(name, value):
    # HPACK literal header field without indexing, new name, no Huffman.
    n = name.encode()
    v = value.encode()
    return b"\x00" + bytes([len(n)]) + n + bytes([len(v)]) + v


def final_block():
    return b"\x88" + lit("content-type", "text/plain")  # :status 200 (static idx 8)


def interim_block(status):
    return lit(":status", status) + lit("link", "</style.css>; rel=preload; as=style")


def send_response(sock, mode, sid):
    if mode == "single":
        sock.sendall(frame(0x1, 0x4, sid, interim_block("103")))
    elif mode == "multi":
        sock.sendall(frame(0x1, 0x4, sid, interim_block("103")))
        sock.sendall(frame(0x1, 0x4, sid, interim_block("103")))
        sock.sendall(frame(0x1, 0x4, sid, interim_block("100")))
    elif mode == "continue":
        sock.sendall(frame(0x1, 0x4, sid, interim_block("100")))
    elif mode == "cont":
        blk = interim_block("103")
        half = len(blk) // 2
        sock.sendall(frame(0x1, 0x0, sid, blk[:half]))  # HEADERS, no END_HEADERS
        sock.sendall(frame(0x9, 0x4, sid, blk[half:]))  # CONTINUATION, END_HEADERS
    # mode "none": no interim
    sock.sendall(frame(0x1, 0x4, sid, final_block()))  # final HEADERS, END_HEADERS
    sock.sendall(frame(0x0, 0x1, sid, BODY))  # DATA, END_STREAM


def handle(sock, mode):
    sock.sendall(frame(0x4, 0x0, 0, b""))  # server SETTINGS
    preface = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    buf = b""
    preface_done = False
    while True:
        data = sock.recv(65535)
        if not data:
            return
        buf += data
        if not preface_done:
            if len(buf) < len(preface):
                continue
            buf = buf[len(preface):]
            preface_done = True
        while len(buf) >= 9:
            ln = int.from_bytes(buf[0:3], "big")
            if len(buf) < 9 + ln:
                break
            ftype = buf[3]
            flags = buf[4]
            sid = int.from_bytes(buf[5:9], "big") & 0x7FFFFFFF
            buf = buf[9 + ln:]
            if ftype == 0x4 and not (flags & 0x1):  # client SETTINGS -> ACK it
                sock.sendall(frame(0x4, 0x1, 0, b""))
            if ftype == 0x1:  # a request HEADERS -> respond on the same stream
                send_response(sock, mode, sid)


def make_cert():
    cert = tempfile.NamedTemporaryFile(suffix=".crt", delete=False).name
    key = tempfile.NamedTemporaryFile(suffix=".key", delete=False).name
    subprocess.run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "3", "-subj",
            "/CN=interim-origin"
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    return cert, key


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("address")
    p.add_argument("port", type=int)
    p.add_argument("--mode", default="single", choices=["single", "multi", "continue", "cont", "none"])
    return p.parse_args()


def main():
    args = parse_args()
    cert, key = make_cert()
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    ctx.set_alpn_protocols(["h2"])
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.address, args.port))
    srv.listen(16)
    print(f"interim h2 origin listening on {args.address}:{args.port} mode={args.mode}", flush=True)
    while True:
        conn, _ = srv.accept()
        try:
            tls = ctx.wrap_socket(conn, server_side=True)
        except Exception as e:
            sys.stderr.write(f"tls error: {e}\n")
            continue
        threading.Thread(target=lambda: handle(tls, args.mode), daemon=True).start()
    return 0


if __name__ == "__main__":
    sys.exit(main())
