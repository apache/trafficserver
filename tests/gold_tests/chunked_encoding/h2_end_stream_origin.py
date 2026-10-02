#!/usr/bin/env python3
"""An HTTP/2 origin that controls how, and when, each response ends its stream."""

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
import sys
import threading
import time

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import RequestReceived

# The body has to fit in the default 65535-byte HTTP/2 windows so it is sent in
# one write, and has to span several 16384-byte DATA frames, each well past the
# 4096-byte water mark the test configures, so the throttle trips before the
# frame that ends the stream is processed.
BODY_SIZE = 60000
# Long enough for ATS to start the response tunnel before the body arrives.
HEADER_PAUSE_SECONDS = 0.5


def queue_body(connection: H2Connection, stream_id: int, path: str) -> None:
    """Queue a body without Content-Length and end the stream the way the path names.

    /data-end-stream:  END_STREAM on the last DATA frame with payload.
    /empty-end-stream: END_STREAM on a separate, empty DATA frame.
    /trailers:         END_STREAM on a trailing HEADERS frame.
    """
    body = b"x" * BODY_SIZE
    frame_size = connection.max_outbound_frame_size
    for offset in range(0, len(body), frame_size):
        last = offset + frame_size >= len(body)
        connection.send_data(stream_id, body[offset:offset + frame_size], end_stream=last and path == "/data-end-stream")
    if path == "/empty-end-stream":
        connection.end_stream(stream_id)
    elif path == "/trailers":
        connection.send_headers(stream_id, [("x-trailer-check", "after-body")], end_stream=True)


def serve_connection(context: ssl.SSLContext, plain_socket: socket.socket) -> None:
    """Answer requests until the peer closes the connection."""
    try:
        tls_socket = context.wrap_socket(plain_socket, server_side=True)
    except (ssl.SSLError, ConnectionError):
        # Ignore readiness probes that never finish TLS.
        plain_socket.close()
        return
    with tls_socket:
        answer_requests(tls_socket)


def answer_requests(tls_socket: ssl.SSLSocket) -> None:
    """Respond to each request on an established HTTP/2 connection."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    tls_socket.sendall(connection.data_to_send())

    while True:
        try:
            data = tls_socket.recv(65535)
        except (ConnectionError, ssl.SSLError):
            return
        if not data:
            return
        for event in connection.receive_data(data):
            if isinstance(event, RequestReceived):
                path = dict(event.headers)[":path"]
                print(f"request_received stream={event.stream_id} path={path}", flush=True)
                connection.send_headers(event.stream_id, [(":status", "200"), ("content-type", "text/plain")])
                tls_socket.sendall(connection.data_to_send())
                time.sleep(HEADER_PAUSE_SECONDS)
                try:
                    queue_body(connection, event.stream_id, path)
                except Exception as error:
                    # Report on stdout, which the test checks, rather than dying quietly in this thread.
                    print(f"queue_body_failed path={path} error={error!r}", flush=True)
                    raise
        # One write keeps the end of the stream right behind the body, so it
        # arrives while the tunnel still has the producer throttled.
        tls_socket.sendall(connection.data_to_send())


def main() -> int:
    """Serve HTTP/2 over TLS on the given port."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.certificate, args.private_key)
    context.set_alpn_protocols(["h2"])

    with socket.create_server(("127.0.0.1", args.port)) as listener:
        while True:
            plain_socket, _ = listener.accept()
            threading.Thread(target=serve_connection, args=(context, plain_socket), daemon=True).start()


if __name__ == "__main__":
    sys.exit(main())
