#!/usr/bin/env python3
'''Origin that answers a POST before reading its body, then drains it.

Models the origin behavior that makes the early-response path interesting: the
response goes out before the request body has been read, the connection is
offered for reuse, and the body is drained afterwards rather than the connection
being closed. That is what nginx does via ngx_http_discard_request_body.
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

import re
import socket
import sys
import threading

EARLY_RESPONSE = (b'HTTP/1.1 420 Be Calm\r\n'
                  b'Content-Length: 0\r\n'
                  b'Connection: keep-alive\r\n'
                  b'\r\n')

# Cacheable but immediately stale, so the next request for it revalidates.
CACHEABLE_BODY = b'cached-body\n'
CACHEABLE_RESPONSE = (
    b'HTTP/1.1 200 OK\r\n'
    b'Cache-Control: max-age=0\r\n'
    b'Last-Modified: Fri, 08 Aug 2025 08:08:08 GMT\r\n'
    b'Content-Length: ' + str(len(CACHEABLE_BODY)).encode() + b'\r\n'
    b'Connection: keep-alive\r\n'
    b'\r\n' + CACHEABLE_BODY)

EARLY_NOT_MODIFIED = (
    b'HTTP/1.1 304 Not Modified\r\n'
    b'Cache-Control: max-age=0\r\n'
    b'Last-Modified: Fri, 08 Aug 2025 08:08:08 GMT\r\n'
    b'Connection: keep-alive\r\n'
    b'\r\n')

REQUEST_LINE = re.compile(rb'^[A-Z]+ \S+ HTTP/1\.[01]\r?$', re.MULTILINE)


def read_head(conn: socket.socket) -> bytes:
    '''Read up to the end of a request head.

    :param conn: Connection to read from.
    :return: The bytes read, empty if the peer closed first.
    '''
    buf = b''
    while b'\r\n\r\n' not in buf:
        try:
            chunk = conn.recv(4096)
        except (OSError, socket.timeout):
            return buf
        if not chunk:
            return buf
        buf += chunk
    return buf


def content_length_of(head: bytes) -> int:
    '''Extract the declared Content-Length from a request head.

    :param head: The request head bytes.
    :return: The declared length, or 0 when absent.
    '''
    match = re.search(rb'\r\ncontent-length:\s*(\d+)', head, re.IGNORECASE)
    return int(match.group(1)) if match else 0


def drain(conn: socket.socket, count: int, conn_id: int) -> None:
    '''Discard count bytes of request body, reporting anything request-shaped.

    :param conn: Connection to drain.
    :param count: Number of bytes to discard.
    :param conn_id: Identifier used in the log lines.
    '''
    seen = b''
    remaining = count
    while remaining > 0:
        try:
            chunk = conn.recv(min(remaining, 4096))
        except (OSError, socket.timeout):
            break
        if not chunk:
            break
        if len(seen) < 512:
            seen += chunk[:512]
        remaining -= len(chunk)

    # If a request line shows up in what was discarded, then bytes that were meant
    # to be a request on this connection were consumed as this POST's body instead.
    found = REQUEST_LINE.search(seen)
    if found:
        print(f'CONN {conn_id} CONSUMED-AS-BODY {found.group(0).decode(errors="replace").strip()}', flush=True)
    print(f'CONN {conn_id} DRAINED {count - max(remaining, 0)} of {count}', flush=True)


def serve(conn: socket.socket, conn_id: int) -> None:
    '''Serve requests on one connection until the peer stops sending.

    :param conn: The accepted connection.
    :param conn_id: Identifier used in the log lines.
    '''
    conn.settimeout(5)
    with conn:
        while True:
            head = read_head(conn)
            if not head:
                return

            first_line = head.split(b'\r\n', 1)[0]
            print(f'CONN {conn_id} REQUEST {first_line.decode(errors="replace")}', flush=True)

            if b' /early' in first_line:
                # Answer before reading the body, then discard it.
                conn.sendall(EARLY_RESPONSE)
                drain(conn, content_length_of(head), conn_id)
            elif b' /revalidate' in first_line:
                if re.search(rb'\r\nif-modified-since:|\r\nif-none-match:', head, re.IGNORECASE):
                    # A revalidation. Answer 304 before reading the body, then
                    # discard it. A 304 builds no response tunnel in ATS, so this
                    # reaches a different release path than /early does.
                    conn.sendall(EARLY_NOT_MODIFIED)
                    drain(conn, content_length_of(head), conn_id)
                else:
                    conn.sendall(CACHEABLE_RESPONSE)
            else:
                conn.sendall(
                    b'HTTP/1.1 200 OK\r\n'
                    b'Content-Length: 0\r\n'
                    b'Connection: keep-alive\r\n'
                    b'X-Origin-Conn: ' + str(conn_id).encode() + b'\r\n'
                    b'\r\n')


def main() -> int:
    '''Accept connections until stopped.

    :return: Process exit status.
    '''
    port = int(sys.argv[1])
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', port))
    listener.listen(16)
    print(f'LISTENING {port}', flush=True)

    conn_id = 0
    while True:
        conn, _ = listener.accept()
        conn_id += 1
        print(f'CONN {conn_id} ACCEPTED', flush=True)
        threading.Thread(target=serve, args=(conn, conn_id), daemon=True).start()


if __name__ == '__main__':
    sys.exit(main())
