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
whole body in one DATA frame and END_STREAM in a second, empty one. Paths
starting with /large-split do the same with a --large-split-size body in two
DATA frames. Paths starting with /late-end get their whole response at once but
END_STREAM only LATE_END_DELAY seconds later, in an empty DATA frame:
/late-end-empty has no body, /late-end-body a short one and /late-end-large a
--large-split-size one. Paths starting with /held-late-end get the same
--large-split-size response as /late-end-large, but HELD_RESPONSE_DELAY seconds
after the request and with END_STREAM HELD_LATE_END_DELAY seconds after that,
except that /held-late-end-nocl has no content-length and gets END_STREAM, in
an empty DATA frame, right after its body. Paths starting with /held-body-nocl
get a response header with no content-length at once, and their
--large-split-size body and an empty END_STREAM DATA frame HELD_RESPONSE_DELAY
seconds later. Paths starting with /end-no-header get an empty END_STREAM DATA
frame and no response header at all. Everything else is answered at once.
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
LATE_END_DELAY = 0.5
# The held tests in test_h2_origin_stalled_stream.py hold the transaction for longer than this, so the
# response arrives while the transaction is held, and END_STREAM for /held-late-end well after.
HELD_RESPONSE_DELAY = 0.2
HELD_LATE_END_DELAY = 1.0


def say(message: str) -> None:
    """Print one line without interleaving lines from other connections."""
    with PRINT_LOCK:
        print(message, flush=True)


def send_response_header(connection: H2Connection, stream_id: int, length: int | None) -> None:
    """Send a 200 response header for a body of length bytes, or with no content-length if length is None."""
    headers = [(":status", "200"), ("cache-control", "no-store")]
    if length is not None:
        headers.append(("content-length", str(length)))
    connection.send_headers(stream_id, headers)


def respond(connection: H2Connection, stream_id: int, path: str) -> None:
    """Answer one stream with a short 200 response."""
    body = f"ok {path}\n".encode()
    send_response_header(connection, stream_id, len(body))
    connection.send_data(stream_id, body, end_stream=True)


def send_without_end(connection: H2Connection, stream_id: int, body: bytes, length: bool = True) -> None:
    """Send a 200 response with its whole body in up to two DATA frames, but without END_STREAM.

    :param length: Whether the response header has a content-length.
    """
    send_response_header(connection, stream_id, len(body) if length else None)
    send_body(connection, stream_id, body)


def send_body(connection: H2Connection, stream_id: int, body: bytes) -> None:
    """Send a body in up to two DATA frames, without END_STREAM."""
    half = (len(body) + 1) // 2
    for piece in (body[:half], body[half:]):
        if piece:
            connection.send_data(stream_id, piece)


def serve_connection(
    tls_socket: ssl.SSLSocket, number: int, slow_delay: float, large_split_size: int, lifecycle: OriginLifecycle
) -> None:
    """Serve one ATS connection until it closes or the test stops the origin."""
    connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
    connection.initiate_connection()
    send_bytes(tls_socket, connection.data_to_send())
    say(f"conn {number} open")

    # Each entry is (next send time, stream id, path, body bytes still to send).
    slow: list[tuple[float, int, str, int]] = []
    # Each entry is (END_STREAM time, stream id, path).
    late: list[tuple[float, int, str]] = []
    # Each entry is (response time, stream id, path).
    held: list[tuple[float, int, str]] = []
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
                    send_response_header(connection, event.stream_id, 100)
                    connection.send_data(event.stream_id, b"x" * 10)
                    continue
                if path.startswith("/large-split"):
                    send_without_end(connection, event.stream_id, b"x" * large_split_size)
                    connection.send_data(event.stream_id, b"", end_stream=True)
                    continue
                if path.startswith("/end-no-header"):
                    # h2 will not send DATA before HEADERS, so write the frame by hand: a zero length, type DATA,
                    # the END_STREAM flag and the stream id.
                    send_bytes(tls_socket, connection.data_to_send())
                    send_bytes(tls_socket, b"\x00\x00\x00\x00\x01" + event.stream_id.to_bytes(4, "big"))
                    say(f"conn {number} ended stream={event.stream_id} path={path}")
                    continue
                if path.startswith("/held-body-nocl"):
                    send_response_header(connection, event.stream_id, None)
                    held.append((time.monotonic() + HELD_RESPONSE_DELAY, event.stream_id, path))
                    continue
                if path.startswith("/held-late-end"):
                    held.append((time.monotonic() + HELD_RESPONSE_DELAY, event.stream_id, path))
                    continue
                if path.startswith("/late-end"):
                    if path.startswith("/late-end-empty"):
                        body = b""
                    elif path.startswith("/late-end-large"):
                        body = b"x" * large_split_size
                    else:
                        body = f"ok {path}\n".encode()
                    send_without_end(connection, event.stream_id, body)
                    late.append((time.monotonic() + LATE_END_DELAY, event.stream_id, path))
                    continue
                if path.startswith("/split"):
                    body = f"ok {path}\n".encode()
                    send_response_header(connection, event.stream_id, len(body))
                    connection.send_data(event.stream_id, body)
                    connection.send_data(event.stream_id, b"", end_stream=True)
                    continue
                if path.startswith("/slow"):
                    pieces = max(1, round(slow_delay / TRICKLE_INTERVAL))
                    send_response_header(connection, event.stream_id, pieces)
                    slow.append((time.monotonic() + TRICKLE_INTERVAL, event.stream_id, path, pieces))
                    continue
                respond(connection, event.stream_id, path)
            elif isinstance(event, StreamReset):
                closed.add(event.stream_id)
                say(f"conn {number} reset stream={event.stream_id} error={int(event.error_code)}")
            elif isinstance(event, ConnectionTerminated):
                say(f"conn {number} goaway error={int(event.error_code)}")

        now = time.monotonic()
        for due in [entry for entry in held if entry[0] <= now]:
            held.remove(due)
            _, stream_id, path = due
            if stream_id in closed:
                continue
            body = b"x" * large_split_size
            if path.startswith("/held-body-nocl"):
                send_body(connection, stream_id, body)
            else:
                send_without_end(connection, stream_id, body, length=not path.startswith("/held-late-end-nocl"))
            say(f"conn {number} answered stream={stream_id} path={path}")
            if path.startswith("/held-late-end") and not path.startswith("/held-late-end-nocl"):
                late.append((now + HELD_LATE_END_DELAY, stream_id, path))
            else:
                connection.send_data(stream_id, b"", end_stream=True)
                say(f"conn {number} ended stream={stream_id} path={path}")
        for due in [entry for entry in late if entry[0] <= now]:
            late.remove(due)
            _, stream_id, path = due
            if stream_id in closed:
                continue
            connection.send_data(stream_id, b"", end_stream=True)
            say(f"conn {number} ended stream={stream_id} path={path}")
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


def run_server(port: int, certificate: str, private_key: str, slow_delay: float, large_split_size: int, stop_file: str) -> int:
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
            serve_connection(tls_socket, connection_number, slow_delay, large_split_size, lifecycle)

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
    parser.add_argument("--large-split-size", type=int, default=20000, help="Body size for the paths with a large body")
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.port, args.certificate, args.private_key, args.slow_delay, args.large_split_size, args.stop_file)


if __name__ == "__main__":
    sys.exit(main())
