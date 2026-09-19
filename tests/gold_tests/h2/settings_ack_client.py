"""Acknowledge dynamic receive windows with concurrent inbound streams."""
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
import ssl
import sys

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, PingAckReceived, RemoteSettingsChanged


def main(port: int) -> None:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.set_alpn_protocols(["h2"])
    connection = H2Connection(config=H2Configuration(client_side=True))
    with socket.create_connection(("127.0.0.1", port), timeout=5) as plain:
        with context.wrap_socket(plain, server_hostname="settings-ack.example.com") as tls:
            connection.initiate_connection()
            tls.sendall(connection.data_to_send())
            settings = 0
            opened = False
            ping_sent = False
            while True:
                data = tls.recv(65535)
                assert data, "ATS closed the connection before acknowledging our PING"
                for event in connection.receive_data(data):
                    if isinstance(event, ConnectionTerminated):
                        raise AssertionError(f"ATS rejected solicited ACKs: {event.error_code}")
                    if isinstance(event, RemoteSettingsChanged):
                        settings += 1
                    if isinstance(event, PingAckReceived):
                        assert settings >= 3, f"Only {settings} SETTINGS frames exercised"
                        print(f"dynamic_settings_acknowledged={settings}", flush=True)
                        return
                if settings and not opened:
                    # The unfinished bodies keep all three streams open. ATS
                    # buffers them before contacting the origin, so the test
                    # depends only on the inbound flow-control exchange.
                    for stream_id in (1, 3, 5):
                        connection.send_headers(
                            stream_id, [
                                (":method", "POST"), (":scheme", "https"), (":authority", "settings-ack.example.com"),
                                (":path", "/"), ("content-length", "1")
                            ],
                            end_stream=False)
                    opened = True
                if settings >= 3 and not ping_sent:
                    # Sent after the queued ACKs; its response proves ATS has
                    # processed them rather than merely queued SETTINGS frames.
                    connection.ping(b"ackcheck")
                    ping_sent = True
                wire = connection.data_to_send()
                if wire:
                    tls.sendall(wire)


if __name__ == "__main__":
    main(int(sys.argv[1]))
