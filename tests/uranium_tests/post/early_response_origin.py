#!/usr/bin/env python3
'''Origin that can answer a POST before reading its body, or honor 100-continue.

Two behaviors are modelled.  On /early the response goes out before the request
body has been read, the connection is offered for reuse, and the body is drained
afterwards rather than the connection being closed.  That is what nginx does via
ngx_http_discard_request_body.  On /complete the origin instead sends a 100
Continue, reads the whole declared body, and answers normally, which is how an
ordinary Expect: 100-continue POST proceeds.

Request lines are logged with a per connection ordinal, so that reuse of a
connection can be asserted without depending on connection numbering.
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

CONTINUE_RESPONSE = b'HTTP/1.1 100 Continue\r\n\r\n'

# A 1xx that is neither 100 nor 103.  ATS dispatches those as final responses, so an origin
# that sends one before reading the body is owed that body for good.
INTERIM_RESPONSE = (b'HTTP/1.1 199 Hold On\r\n'
                    b'Content-Length: 0\r\n'
                    b'Connection: keep-alive\r\n'
                    b'\r\n')

REQUEST_LINE = re.compile(rb'[A-Z]+ \S+ HTTP/1\.[01]\r?\n')


def read_head(conn: socket.socket) -> tuple[bytes, bool]:
    '''Read up to the end of a request head.

    :param conn: Connection to read from.
    :return: The bytes read, and whether a complete head was seen.
    '''
    buf = b''
    while b'\r\n\r\n' not in buf:
        try:
            chunk = conn.recv(4096)
        except (OSError, socket.timeout):
            return buf, False
        if not chunk:
            return buf, False
        buf += chunk
    return buf, True


def content_length_of(head: bytes) -> int:
    '''Extract the declared Content-Length from a request head.

    :param head: The request head bytes.
    :return: The declared length, or 0 when absent.
    '''
    match = re.search(rb'\r\ncontent-length:\s*(\d+)', head, re.IGNORECASE)
    return int(match.group(1)) if match else 0


def drain(conn: socket.socket, count: int, conn_id: int) -> None:
    '''Discard count bytes of request body, reporting anything request shaped.

    A request line found inside these bytes means bytes that were meant to be a
    request on this connection were consumed as this POST's body instead, which is
    the framing error this guards against.  It is reported as soon as it is seen rather
    than after the drain finishes, because on a broken build the drain cannot
    complete and only ends on a timeout.

    :param conn: Connection to drain.
    :param count: Number of bytes to discard.
    :param conn_id: Identifier used in the log lines.
    '''
    window = b''
    remaining = count
    while remaining > 0:
        try:
            chunk = conn.recv(min(remaining, 4096))
        except (OSError, socket.timeout):
            break
        if not chunk:
            break
        remaining -= len(chunk)

        window += chunk
        found = REQUEST_LINE.search(window)
        if found:
            print(f'CONN {conn_id} CONSUMED-AS-BODY {found.group(0).decode(errors="replace").strip()}', flush=True)
            window = b''
        else:
            # Keep enough of a tail that a request line straddling two reads is still found.
            window = window[-1024:]

    print(f'CONN {conn_id} DRAINED {count - max(remaining, 0)} of {count}', flush=True)


def read_body(conn: socket.socket, count: int) -> int:
    '''Read exactly count body bytes.

    :param conn: Connection to read from.
    :param count: Number of bytes expected.
    :return: The number of bytes actually read.
    '''
    remaining = count
    while remaining > 0:
        try:
            chunk = conn.recv(min(remaining, 4096))
        except (OSError, socket.timeout):
            break
        if not chunk:
            break
        remaining -= len(chunk)
    return count - remaining


def serve(conn: socket.socket, conn_id: int) -> None:
    '''Serve requests on one connection until the peer stops sending.

    :param conn: The accepted connection.
    :param conn_id: Identifier used in the log lines.
    '''
    conn.settimeout(10)
    ordinal = 0
    with conn:
        while True:
            head, complete = read_head(conn)
            if not complete:
                return

            ordinal += 1
            request_line = head.split(b'\r\n', 1)[0].decode(errors='replace')
            target = head.split(b'\r\n', 1)[0]
            declared = content_length_of(head)
            print(f'CONN {conn_id} REQUEST {ordinal} {request_line}', flush=True)
            if re.search(rb'\r\nexpect:\s*100-continue', head, re.IGNORECASE):
                print(f'CONN {conn_id} SAW-EXPECT {ordinal}', flush=True)

            if b' /early' in target:
                # Answer before reading the body, then discard it.
                conn.sendall(EARLY_RESPONSE)
                drain(conn, declared, conn_id)
            elif b' /interim' in target:
                # A 1xx outside the interim set, sent before reading the body.  ATS treats
                # this as a final response, so the declared body is never coming.
                conn.sendall(INTERIM_RESPONSE)
                drain(conn, declared, conn_id)
            elif b' /complete' in target:
                # Behave the way an ordinary Expect: 100-continue POST expects.
                conn.sendall(CONTINUE_RESPONSE)
                received = read_body(conn, declared)
                print(f'CONN {conn_id} BODY-COMPLETE {received} of {declared}', flush=True)
                conn.sendall(b'HTTP/1.1 200 OK\r\n'
                             b'Content-Length: 0\r\n'
                             b'Connection: keep-alive\r\n'
                             b'\r\n')
            else:
                conn.sendall(b'HTTP/1.1 200 OK\r\n'
                             b'Content-Length: 0\r\n'
                             b'Connection: keep-alive\r\n'
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
