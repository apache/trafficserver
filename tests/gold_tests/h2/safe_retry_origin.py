#!/usr/bin/env python3
"""Exercise safe retries and graceful drains after HTTP/2 REFUSED_STREAM and GOAWAY."""

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
import select
import ssl
import sys
import threading

from origin_lifecycle import OriginLifecycle, send_bytes

from hyperframe.frame import ContinuationFrame, GoAwayFrame, SettingsFrame

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.errors import ErrorCodes
from h2.events import ConnectionTerminated, DataReceived, RequestReceived, StreamEnded

EXPECTED_BODY = b"request-body"
RESPONSE_BODY = b"retried"
DRAINED_BODY = b"drained"
MAX_STREAM_ID = 2**31 - 1
# goaway-split holds this many multiplexed requests before its GOAWAY covers
# only the lowest, so that two adjacent streams are closed in one pass.
SPLIT_REQUESTS = 3


class RetryOrigin:
    """An HTTP/2 origin that rejects the first POST and accepts its retry."""

    def __init__(self, mode: str, lifecycle: OriginLifecycle) -> None:
        self.mode = mode
        self.lifecycle = lifecycle
        self.attempts = 0
        self.extra_bytes = b""
        # goaway-split serves its retries on concurrent connections.
        self.lock = threading.Lock()

    def _validate_request(self, headers: dict[str, str], body: bytes) -> None:
        """Verify that ATS replayed the non-idempotent request intact."""
        if headers.get(":method") != "POST":
            raise RuntimeError(f"expected POST, got {headers.get(':method')}")
        if body != EXPECTED_BODY:
            raise RuntimeError(f"expected body {EXPECTED_BODY!r}, got {body!r}")

    def _drain_first_attempt(self, connection: H2Connection, stream_id: int) -> bytes:
        """Announce a graceful shutdown and answer the request if the GOAWAY covers it."""
        last_stream_id = {"goaway-drain-max": MAX_STREAM_ID, "goaway-drain-above": 0}.get(self.mode, stream_id)
        # h2 refuses to send on a connection after its own GOAWAY, so frame it by hand.
        goaway = GoAwayFrame(0)
        goaway.last_stream_id = last_stream_id
        goaway.error_code = ErrorCodes.NO_ERROR
        print(f"action=GOAWAY attempt=1 last_stream_id={last_stream_id}", flush=True)
        if stream_id <= last_stream_id:
            connection.send_headers(stream_id, [(":status", "200"), ("content-length", str(len(DRAINED_BODY)))])
            connection.send_data(stream_id, DRAINED_BODY, end_stream=True)
        return goaway.serialize()

    def _reject_first_attempt(self, connection: H2Connection, stream_id: int) -> bool:
        """Reject the first request and return whether to close the socket."""
        if self.mode == "unsolicited-continuation":
            connection.send_headers(stream_id, [(":status", "100")])
            frame = ContinuationFrame(stream_id)
            frame.flags.add("END_HEADERS")
            frame.data = b"\x88"
            self.extra_bytes = frame.serialize()
            return False
        if self.mode == "settings-flood":
            self.extra_bytes = SettingsFrame().serialize() * 3
            return False
        if self.mode in ("rst", "early-rst", "rst-internal", "rst-cancel", "response-rst"):
            if self.mode == "response-rst":
                connection.send_headers(stream_id, [(":status", "100")])
            error = {
                "rst-internal": ErrorCodes.INTERNAL_ERROR,
                "rst-cancel": ErrorCodes.CANCEL
            }.get(self.mode, ErrorCodes.REFUSED_STREAM)
            connection.reset_stream(stream_id, error_code=error)
            print(f"action=RST_STREAM attempt=1 error={error.name}", flush=True)
            return False

        connection.close_connection(
            error_code=ErrorCodes.NO_ERROR,
            last_stream_id=stream_id if self.mode == "goaway-equal" else 0,
        )
        print(f"action=GOAWAY attempt=1 last_stream_id={stream_id if self.mode == 'goaway-equal' else 0}", flush=True)
        return True

    def _wait_for_close(self, tls_socket: ssl.SSLSocket) -> None:
        """Report how ATS ends the drained connection: an orderly close, a reset, or a TLS error."""
        while not self.lifecycle.stopped:
            try:
                data = tls_socket.recv(65535)
            except TimeoutError:
                continue
            except (ssl.SSLZeroReturnError, ssl.SSLEOFError):
                data = b""
            except ConnectionResetError:
                print("drained_session_reset_by_peer", flush=True)
                return
            except ssl.SSLError as error:
                print(f"drained_session_tls_error {error}", flush=True)
                return
            if not data:
                print("drained_session_closed_by_peer", flush=True)
                return

    def serve_connection(self, tls_socket: ssl.SSLSocket, listener: socket.socket | None) -> bool:
        """Serve one HTTP/2 connection and report whether the retry succeeded."""
        connection = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
        connection.initiate_connection()
        send_bytes(tls_socket, connection.data_to_send())

        headers_by_stream: dict[int, dict[str, str]] = {}
        bodies_by_stream: dict[int, bytearray] = {}
        split_streams: list[int] = []
        split = self.mode == "goaway-split"

        while not self.lifecycle.stopped:
            if not tls_socket.pending():
                # Without a listener, concurrent connections are expected and the main thread accepts them.
                watched = [tls_socket] if listener is None else [tls_socket, listener]
                readable, _, _ = select.select(watched, [], [], 0.2)
                if listener is not None and listener in readable:
                    raise AssertionError("Unexpected fresh origin connection while the reusable connection is still open")
                if tls_socket not in readable:
                    continue
            try:
                data = tls_socket.recv(65535)
            except TimeoutError:
                continue
            if not data:
                return False

            close_socket = False
            retry_succeeded = False
            drain_succeeded = False
            goaway_bytes = b""
            for event in connection.receive_data(data):
                if isinstance(event, RequestReceived):
                    print(f"request_received attempt={self.attempts + 1}", flush=True)
                    headers_by_stream[event.stream_id] = dict(event.headers)
                    bodies_by_stream[event.stream_id] = bytearray()
                    if self.mode == "early-rst" and self.attempts == 0:
                        self.attempts += 1
                        self._reject_first_attempt(connection, event.stream_id)
                        send_bytes(tls_socket, connection.data_to_send())
                        return False
                elif isinstance(event, DataReceived):
                    bodies_by_stream[event.stream_id].extend(event.data)
                    connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                elif isinstance(event, StreamEnded):
                    headers = headers_by_stream[event.stream_id]
                    body = bytes(bodies_by_stream[event.stream_id])
                    self._validate_request(headers, body)
                    with self.lock:
                        self.attempts += 1
                        attempt = self.attempts

                    if split and attempt <= SPLIT_REQUESTS:
                        # Hold the multiplexed requests, then drain the lowest
                        # stream and leave the higher ones unprocessed.
                        split_streams.append(event.stream_id)
                        if len(split_streams) == SPLIT_REQUESTS:
                            goaway_bytes = self._drain_first_attempt(connection, min(split_streams))
                            drain_succeeded = True
                    elif attempt == 1 and self.mode.startswith("goaway-drain"):
                        goaway_bytes = self._drain_first_attempt(connection, event.stream_id)
                        drain_succeeded = True
                    elif attempt == 1:
                        close_socket = self._reject_first_attempt(connection, event.stream_id)
                    elif attempt == 2 or (split and attempt > SPLIT_REQUESTS):
                        connection.send_headers(
                            event.stream_id,
                            [
                                (":status", "200"),
                                ("content-length", str(len(RESPONSE_BODY))),
                            ],
                        )
                        connection.send_data(event.stream_id, RESPONSE_BODY, end_stream=True)
                        retry_succeeded = True
                    else:
                        raise RuntimeError(f"received unexpected request attempt {attempt}")
                elif isinstance(event, ConnectionTerminated):
                    print(f"peer_goaway error={event.error_code}", flush=True)
                    close_socket = True

            wire_bytes = goaway_bytes + connection.data_to_send() + self.extra_bytes
            self.extra_bytes = b""
            if wire_bytes:
                if close_socket and self.mode == "goaway-reserved":
                    # Set the reserved bit on the already serialized last-stream ID.
                    wire = bytearray(wire_bytes)
                    offset = 0
                    while offset + 9 <= len(wire):
                        length = int.from_bytes(wire[offset:offset + 3], "big")
                        if wire[offset + 3] == 7:
                            wire[offset + 9] |= 0x80
                        offset += 9 + length
                    wire_bytes = bytes(wire)
                send_bytes(tls_socket, wire_bytes)

            if drain_succeeded:
                print(f"drain_succeeded attempts={attempt} method=POST", flush=True)
                # Leave the connection open: ATS, not the origin, must close a drained session.
                self._wait_for_close(tls_socket)
                return True
            if retry_succeeded:
                print(
                    f"retry_succeeded attempts={attempt} method=POST body={EXPECTED_BODY.decode()}",
                    flush=True,
                )
                if not split:
                    return True
            if close_socket:
                return False

        return False


def run_server(mode: str, port: int, certificate: str, private_key: str, stop_file: str, expected_attempts: int) -> int:
    """Serve until the client and retry-counter assertions finish."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, private_key)
    context.set_alpn_protocols(["h2"])

    lifecycle = OriginLifecycle(stop_file)
    origin = RetryOrigin(mode, lifecycle)
    errors: list[BaseException] = []

    def serve_socket(plain_socket: socket.socket, listener: socket.socket | None) -> None:
        plain_socket.settimeout(5)
        try:
            tls_socket = context.wrap_socket(plain_socket, server_side=True)
        except (ssl.SSLError, TimeoutError):
            # Ignore readiness probes and peers that never finish TLS.
            plain_socket.close()
            return
        with tls_socket:
            if tls_socket.selected_alpn_protocol() != "h2":
                raise RuntimeError("ATS did not negotiate HTTP/2 with the origin")
            tls_socket.settimeout(0.2)
            origin.serve_connection(tls_socket, listener)

    def serve_socket_in_thread(plain_socket: socket.socket) -> None:
        try:
            serve_socket(plain_socket, None)
        except BaseException as error:
            errors.append(error)

    threads: list[threading.Thread] = []
    with socket.create_server(("127.0.0.1", port)) as listener:
        listener.settimeout(0.2)
        while not lifecycle.stopped:
            try:
                plain_socket, _ = listener.accept()
            except TimeoutError:
                continue
            if mode == "goaway-split":
                # The retries of the unprocessed streams may each open a connection.
                threads.append(threading.Thread(target=serve_socket_in_thread, args=(plain_socket,)))
                threads[-1].start()
            else:
                serve_socket(plain_socket, listener)

    for thread in threads:
        thread.join()
    if errors:
        raise errors[0]
    assert origin.attempts == expected_attempts, f"Expected {expected_attempts} attempts, got {origin.attempts}"
    lifecycle.complete()
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=(
            "rst", "early-rst", "goaway", "rst-internal", "rst-cancel", "response-rst", "goaway-equal", "goaway-reserved",
            "goaway-drain", "goaway-drain-max", "goaway-drain-above", "goaway-split", "unsolicited-continuation", "settings-flood"))
    parser.add_argument("port", type=int)
    parser.add_argument("certificate")
    parser.add_argument("private_key")
    parser.add_argument("stop_file")
    parser.add_argument("expected_attempts", type=int)
    return parser.parse_args()


def main() -> int:
    """Run the test origin."""
    args = parse_args()
    return run_server(args.mode, args.port, args.certificate, args.private_key, args.stop_file, args.expected_attempts)


if __name__ == "__main__":
    sys.exit(main())
