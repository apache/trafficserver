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
'''
Origin that answers with one chunk of body, then withholds the last-chunk.

The body arrives in a single read so ATS dechunks all of it at once. The
terminal "0\\r\\n\\r\\n" is sent --hold seconds later, on its own, so ATS reads
it while the stalled client still has a backlog and parks it unparsed. Proxy
Verifier cannot delay part of an HTTP/1.1 body, hence this custom origin.
'''

import argparse
import socket
import sys
import threading
import time


def read_request(conn):
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = conn.recv(4096)
        if not chunk:
            return None
        buf += chunk
    head, _, body = buf.partition(b"\r\n\r\n")
    content_length = 0
    for line in head.split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"content-length":
            content_length = int(value.strip())
    while len(body) < content_length:
        chunk = conn.recv(4096)
        if not chunk:
            return None
        body += chunk
    return head


def handle(conn, body_size, hold):
    try:
        conn.settimeout(60)
        if read_request(conn) is None:
            return
        # Header and the one data chunk arrive together so ATS parses the whole
        # chunk into the dechunked buffer, filling it past the water mark while
        # the stalled client holds the consumer above high water.
        conn.sendall(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: application/octet-stream\r\n"
            b"Transfer-Encoding: chunked\r\n"
            b"\r\n" + b"%x\r\n" % body_size + b"x" * body_size + b"\r\n")
        # The last-chunk arrives as its own read while the consumer is throttled,
        # so the chunked throttle parks it unparsed in chunked_reader.
        time.sleep(hold)
        conn.sendall(b"0\r\n\r\n")
        sys.stderr.write("origin: sent last-chunk\n")
        sys.stderr.flush()
        while conn.recv(4096):
            pass
    except (OSError, socket.timeout):
        return
    finally:
        try:
            conn.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--ip", default="127.0.0.1")
    ap.add_argument("--body-size", type=int, default=24000)
    ap.add_argument("--hold", type=float, default=0.5, help="seconds between the body and the last-chunk")
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.ip, args.port))
    srv.listen(128)
    sys.stderr.write(f"dechunk half-close origin listening on {args.ip}:{args.port}\n")
    sys.stderr.flush()

    while True:
        try:
            conn, _ = srv.accept()
        except OSError:
            break
        threading.Thread(target=handle, args=(conn, args.body_size, args.hold), daemon=True).start()


if __name__ == "__main__":
    main()
