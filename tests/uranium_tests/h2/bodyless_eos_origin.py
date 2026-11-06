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
"""Origins for the bodyless HTTP/2 response error.log test.

An HTTP/2 origin answers each request path with a response that ends on its
HEADERS frame, or resets the stream before sending any header. An HTTP/1.1
origin sends a response header cut off at a line boundary and then closes.
"""

import argparse
import socket
import ssl
import sys
import threading
import time

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.errors import ErrorCodes
from h2.events import ConnectionTerminated, RequestReceived, StreamEnded

# The final response header for each path, sent with END_STREAM on its HEADERS frame.
BODYLESS_RESPONSES: dict[str, list[tuple[str, str]]] = {
    # ATS sends HEAD for this path, so the Content-Length describes a body that is never sent.
    "/head": [(":status", "200"), ("content-length", "5")],
    "/no-content": [(":status", "204")],
    "/not-modified": [(":status", "304")],
    "/not-modified-with-length": [(":status", "304"), ("content-length", "5")],
    "/empty-ok": [(":status", "200")],
    "/redirect-zero-length": [(":status", "302"), ("location", "/elsewhere"), ("content-length", "0")],
    "/early-hints-no-content": [(":status", "204")],
}


def send(tls_socket: ssl.SSLSocket, connection: H2Connection) -> None:
    """Write the frames the HTTP/2 state machine has queued."""
    wire = connection.data_to_send()
    if wire:
        tls_socket.sendall(wire)


def respond(tls_socket: ssl.SSLSocket, connection: H2Connection, stream_id: int, path: str) -> None:
    """Answer one request according to its path."""
    if path == "/reset-before-headers":
        connection.reset_stream(stream_id, error_code=ErrorCodes.INTERNAL_ERROR)
    elif path in BODYLESS_RESPONSES:
        if path == "/early-hints-no-content":
            connection.send_headers(stream_id, [(":status", "103"), ("link", "</style.css>; rel=preload; as=style")])
            send(tls_socket, connection)
        connection.send_headers(stream_id, BODYLESS_RESPONSES[path], end_stream=True)
    else:
        connection.send_headers(stream_id, [(":status", "404"), ("content-length", "0")], end_stream=True)
    send(tls_socket, connection)
    print(f"h2 answered {path}", flush=True)


def serve_h2_connection(tls_socket: ssl.SSLSocket) -> None:
    """Serve HTTP/2 requests until ATS closes the connection."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    send(tls_socket, connection)
    paths: dict[int, str] = {}
    try:
        while data := tls_socket.recv(65535):
            for event in connection.receive_data(data):
                if isinstance(event, ConnectionTerminated):
                    return
                if isinstance(event, RequestReceived):
                    paths[event.stream_id] = dict(event.headers)[":path"]
                elif isinstance(event, StreamEnded):
                    respond(tls_socket, connection, event.stream_id, paths.pop(event.stream_id))
            send(tls_socket, connection)
    except (OSError, ssl.SSLError):
        return
    finally:
        tls_socket.close()


def serve_h2(listener: socket.socket, context: ssl.SSLContext) -> None:
    """Accept TLS connections and serve each on its own thread."""
    while True:
        connection, _ = listener.accept()
        try:
            tls_socket = context.wrap_socket(connection, server_side=True)
        except (OSError, ssl.SSLError) as error:
            print(f"h2 TLS handshake failed: {error}", file=sys.stderr, flush=True)
            connection.close()
            continue
        threading.Thread(target=serve_h2_connection, args=(tls_socket,), daemon=True).start()


def serve_h1_connection(connection: socket.socket) -> None:
    """Send a header that ends after a complete field line, then close.

    The pause separates the header bytes from the close, so that ATS reads
    the partial header before it sees EOS.
    """
    with connection:
        request = b""
        while b"\r\n\r\n" not in request:
            data = connection.recv(65535)
            if not data:
                return
            request += data
        connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n")
        time.sleep(0.5)
        print("h1 sent a header cut at a line boundary", flush=True)


def serve_h1(listener: socket.socket) -> None:
    """Accept HTTP/1.1 connections and serve each on its own thread."""
    while True:
        connection, _ = listener.accept()
        threading.Thread(target=serve_h1_connection, args=(connection,), daemon=True).start()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("h2_port", type=int, help="TLS port of the HTTP/2 origin")
    parser.add_argument("h1_port", type=int, help="Plain-text port of the HTTP/1.1 origin")
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.certificate, args.private_key)
    context.set_alpn_protocols(["h2"])

    # Bind the HTTP/1.1 listener first: the test waits only for the HTTP/2 port.
    h1_listener = socket.create_server(("127.0.0.1", args.h1_port))
    h2_listener = socket.create_server(("127.0.0.1", args.h2_port))
    threading.Thread(target=serve_h1, args=(h1_listener,), daemon=True).start()
    print(f"listening h2={args.h2_port} h1={args.h1_port}", flush=True)
    serve_h2(h2_listener, context)
    return 0


if __name__ == "__main__":
    sys.exit(main())
