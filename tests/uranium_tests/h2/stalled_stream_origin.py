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
"""HTTP/2 origin that never answers some streams and reports what ATS does with them.

Paths starting with /stall are never answered. Paths starting with /partial
get their response header and part of their body, then nothing more. Paths
starting with /slow get their response header at once and their body one byte
at a time, every TRICKLE_INTERVAL seconds, over --slow-delay seconds, so the
stream stays busy without going idle. Paths starting with /split get their
whole body in one DATA frame and END_STREAM in a second, empty one. Everything
else is answered at once.
Every connection is numbered, and each line it prints starts with "conn N".
"""

import argparse
import socket
import ssl
import sys
import threading
import time

from origin_lifecycle import OriginLifecycle, send_bytes

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, RequestReceived, StreamReset

PRINT_LOCK = threading.Lock()
TRICKLE_INTERVAL = 0.5


def say(message: str) -> None:
    """Print one line without interleaving lines from other connections."""
    with PRINT_LOCK:
        print(message, flush=True)


def respond(connection: H2Connection, stream_id: int, path: str) -> None:
    """Answer one stream with a short 200 response."""
    body = f"ok {path}\n".encode()
    connection.send_headers(stream_id, [(":status", "200"), ("content-length", str(len(body))), ("cache-control", "no-store")])
    connection.send_data(stream_id, body, end_stream=True)


def serve_connection(tls_socket: ssl.SSLSocket, number: int, slow_delay: float, lifecycle: OriginLifecycle) -> None:
    """Serve one ATS connection until it closes or the test stops the origin."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    send_bytes(tls_socket, connection.data_to_send())
    say(f"conn {number} open")

    # Each entry is (next send time, stream id, path, body bytes still to send).
    slow: list[tuple[float, int, str, int]] = []
    closed: set[int] = set()
    while not lifecycle.stopped:
        try:
            data = tls_socket.recv(65535)
        except TimeoutError:
            data = None
        except (ConnectionError, ssl.SSLError) as error:
            say(f"conn {number} eof {error!r}")
            return
        if data == b"":
            say(f"conn {number} eof")
            return

        for event in connection.receive_data(data) if data else []:
            if isinstance(event, RequestReceived):
                path = dict(event.headers).get(":path", "/")
                say(f"conn {number} request stream={event.stream_id} path={path}")
                if path.startswith("/stall"):
                    continue
                if path.startswith("/partial"):
                    connection.send_headers(
                        event.stream_id, [(":status", "200"), ("content-length", "100"), ("cache-control", "no-store")]
                    )
                    connection.send_data(event.stream_id, b"x" * 10)
                    continue
                if path.startswith("/split"):
                    body = f"ok {path}\n".encode()
                    connection.send_headers(
                        event.stream_id, [(":status", "200"), ("content-length", str(len(body))), ("cache-control", "no-store")]
                    )
                    connection.send_data(event.stream_id, body)
                    connection.send_data(event.stream_id, b"", end_stream=True)
                    continue
                if path.startswith("/slow"):
                    pieces = max(1, round(slow_delay / TRICKLE_INTERVAL))
                    connection.send_headers(
                        event.stream_id, [(":status", "200"), ("content-length", str(pieces)), ("cache-control", "no-store")]
                    )
                    slow.append((time.monotonic() + TRICKLE_INTERVAL, event.stream_id, path, pieces))
                    continue
                respond(connection, event.stream_id, path)
            elif isinstance(event, StreamReset):
                closed.add(event.stream_id)
                say(f"conn {number} reset stream={event.stream_id} error={int(event.error_code)}")
            elif isinstance(event, ConnectionTerminated):
                say(f"conn {number} goaway error={int(event.error_code)}")

        now = time.monotonic()
        for due in [entry for entry in slow if entry[0] <= now]:
            slow.remove(due)
            _, stream_id, path, left = due
            if stream_id in closed:
                continue
            connection.send_data(stream_id, b".", end_stream=left == 1)
            if left == 1:
                say(f"conn {number} answered stream={stream_id} path={path}")
            else:
                slow.append((now + TRICKLE_INTERVAL, stream_id, path, left - 1))

        wire = connection.data_to_send()
        if wire:
            send_bytes(tls_socket, wire)


def run_server(port: int, certificate: str, private_key: str, slow_delay: float, stop_file: str) -> int:
    """Accept ATS connections, each on its own thread, until the test asks the origin to stop."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, private_key)
    context.set_alpn_protocols(["h2"])

    lifecycle = OriginLifecycle(stop_file)
    threads: list[threading.Thread] = []
    opened = [0]
    count_lock = threading.Lock()

    def handle(plain_socket: socket.socket) -> None:
        plain_socket.settimeout(5)
        try:
            tls_socket = context.wrap_socket(plain_socket, server_side=True)
        except (ssl.SSLError, TimeoutError, ConnectionError) as error:
            # Readiness probes never finish TLS, so this is not a failure on its own.
            say(f"handshake failed {error!r}")
            plain_socket.close()
            return
        with count_lock:
            opened[0] += 1
            connection_number = opened[0]
        with tls_socket:
            if tls_socket.selected_alpn_protocol() != "h2":
                say(f"conn {connection_number} not_h2")
                return
            tls_socket.settimeout(0.1)
            serve_connection(tls_socket, connection_number, slow_delay, lifecycle)

    with socket.create_server(("127.0.0.1", port)) as listener:
        listener.settimeout(0.2)
        while not lifecycle.stopped:
            try:
                plain_socket, _ = listener.accept()
            except TimeoutError:
                continue
            thread = threading.Thread(target=handle, args=(plain_socket,), daemon=True)
            thread.start()
            threads.append(thread)

    for thread in threads:
        thread.join(timeout=5)
    lifecycle.complete()
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    parser.add_argument("stop_file")
    parser.add_argument("--slow-delay", type=float, default=1.5)
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.port, args.certificate, args.private_key, args.slow_delay, args.stop_file)


if __name__ == "__main__":
    sys.exit(main())
