"""A cacheable origin that logs every request path it receives.

Used by delete_maxforwards_body_desync.test.py. It serves a cacheable 200 for
any GET so the proxy can turn a warmed path into a cache hit, and it prints a
distinctive marker for every request it receives so the test can assert that a
smuggled request never reaches the origin.
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
import sys
import threading


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", help="Address to listen on.")
    parser.add_argument("port", type=int, help="The port to listen on.")
    return parser.parse_args()


def handle(conn: socket.socket) -> None:
    conn.settimeout(30)
    buf = b""
    try:
        while True:
            while b"\r\n\r\n" not in buf:
                chunk = conn.recv(65536)
                if not chunk:
                    return
                buf += chunk
            head, buf = buf.split(b"\r\n\r\n", 1)
            lines = head.split(b"\r\n")
            request_line = lines[0].decode("latin1", "replace")
            parts = request_line.split(" ")
            path = parts[1] if len(parts) > 1 else "/"
            # Distinctive, greppable marker for the test's assertions.
            print(f"ORIGIN_RECV path=[{path}]", flush=True)

            # Drain a declared request body so keep-alive framing stays intact.
            content_length = 0
            for h in lines[1:]:
                if h.lower().startswith(b"content-length:"):
                    try:
                        content_length = int(h.split(b":", 1)[1].strip())
                    except ValueError:
                        content_length = 0
            while len(buf) < content_length:
                chunk = conn.recv(65536)
                if not chunk:
                    break
                buf += chunk
            buf = buf[content_length:]

            body = f"origin-response path={path}\n".encode()
            resp = (
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: text/plain\r\n"
                b"Cache-Control: public, max-age=300\r\n"
                b"Content-Length: %d\r\n\r\n" % len(body)) + body
            conn.sendall(resp)
    except OSError:
        pass
    finally:
        conn.close()


def main() -> int:
    args = parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.address, args.port))
    sock.listen(16)
    print(f"Listening on {args.address}:{args.port}", flush=True)
    while True:
        conn, _ = sock.accept()
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    sys.exit(main())
