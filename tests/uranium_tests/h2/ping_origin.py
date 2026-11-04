#!/usr/bin/env python3
"""Serve an HTTP/2 origin that sends a burst of PING frames before each response."""

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

from origin_lifecycle import OriginLifecycle, send_bytes

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, PingAckReceived, RequestReceived


def serve_connection(tls_socket: ssl.SSLSocket, ping_count: int, lifecycle: OriginLifecycle) -> None:
    """Answer each request only after ATS has acknowledged every PING sent for it."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    send_bytes(tls_socket, connection.data_to_send())

    waiting: list[int] = []
    acks = 0
    while not lifecycle.stopped:
        try:
            data = tls_socket.recv(65535)
        except TimeoutError:
            continue
        if not data:
            return

        for event in connection.receive_data(data):
            if isinstance(event, ConnectionTerminated):
                print(f"peer_goaway error={event.error_code} ping_acks={acks}", flush=True)
                return
            if isinstance(event, RequestReceived):
                print(f"request_received stream={event.stream_id}", flush=True)
                waiting.append(event.stream_id)
                for index in range(ping_count):
                    connection.ping(index.to_bytes(8, "big"))
            elif isinstance(event, PingAckReceived):
                acks += 1
                if acks % ping_count == 0:
                    stream_id = waiting.pop(0)
                    print(f"ping_acks={acks}", flush=True)
                    connection.send_headers(stream_id, [(":status", "200"), ("content-length", "2")])
                    connection.send_data(stream_id, b"ok", end_stream=True)
        wire = connection.data_to_send()
        if wire:
            send_bytes(tls_socket, wire)


def run_server(port: int, certificate: str, private_key: str, ping_count: int, stop_file: str) -> int:
    """Serve until the test asks the origin to stop."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, private_key)
    context.set_alpn_protocols(["h2"])

    lifecycle = OriginLifecycle(stop_file)
    with socket.create_server(("127.0.0.1", port)) as listener:
        listener.settimeout(0.2)
        while not lifecycle.stopped:
            try:
                plain_socket, _ = listener.accept()
            except TimeoutError:
                continue
            plain_socket.settimeout(5)
            try:
                tls_socket = context.wrap_socket(plain_socket, server_side=True)
            except (ssl.SSLError, TimeoutError):
                # Ignore readiness probes and peers that never finish TLS.
                plain_socket.close()
                continue
            with tls_socket:
                if tls_socket.selected_alpn_protocol() != "h2":
                    raise RuntimeError("ATS did not negotiate HTTP/2 with the origin")
                tls_socket.settimeout(0.2)
                serve_connection(tls_socket, ping_count, lifecycle)

    lifecycle.complete()
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    parser.add_argument("ping_count", type=int)
    parser.add_argument("stop_file")
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.port, args.certificate, args.private_key, args.ping_count, args.stop_file)


if __name__ == "__main__":
    sys.exit(main())
