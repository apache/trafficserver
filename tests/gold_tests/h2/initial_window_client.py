"""Check inbound HTTP/2 stream and connection receive windows on the wire."""
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

import socket
import sys

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, PingAckReceived


def synchronize(connection: H2Connection, sock: socket.socket, sequence: int) -> None:
    """Wait for a PING response after previously sent frames are processed."""
    token = sequence.to_bytes(8, "big")
    connection.ping(token)
    sock.sendall(connection.data_to_send())
    while True:
        data = sock.recv(65535)
        assert data, "ATS closed the connection during window validation"
        events = connection.receive_data(data)
        for event in events:
            if isinstance(event, ConnectionTerminated):
                raise AssertionError(f"Unexpected GOAWAY: {event.error_code}")
        wire = connection.data_to_send()
        if wire:
            sock.sendall(wire)
        if any(isinstance(event, PingAckReceived) and event.ping_data == token for event in events):
            return


def main(port: int, policy: int, window: int) -> None:
    connection = H2Connection(config=H2Configuration(client_side=True))
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        connection.initiate_connection()
        synchronize(connection, sock, 0)
        assert connection.remote_settings.initial_window_size == window, (
            f"preface stream window {connection.remote_settings.initial_window_size} != {window}")
        session_window = window if policy == 0 else window * 100
        # HTTP/2 cannot shrink the initial connection window with WINDOW_UPDATE.
        assert connection.outbound_flow_control_window == max(
            65535,
            session_window), (f"connection window {connection.outbound_flow_control_window} != {max(65535, session_window)}")
        for count, stream_id in enumerate((1, 3, 5), start=1):
            # Buffered, unfinished bodies keep streams open without an origin.
            connection.send_headers(
                stream_id, [
                    (":method", "POST"), (":scheme", "http"), (":authority", "window.example"), (":path", "/"),
                    ("content-length", "1")
                ],
                end_stream=False)
            synchronize(connection, sock, count)
            expected = session_window // count if policy == 2 else window
            assert connection.remote_settings.initial_window_size == expected, (
                f"{count} streams: window {connection.remote_settings.initial_window_size} != {expected}")
            for active_id in range(1, stream_id + 1, 2):
                assert connection.streams[active_id].outbound_flow_control_window == expected
        print(f"verified policy={policy} stream_window={window} connection_window={max(65535, session_window)}")


if __name__ == "__main__":
    main(*(int(value) for value in sys.argv[1:]))
