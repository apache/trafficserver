#!/usr/bin/env python3
"""Send ATS either a burst of PING frames or one GET over a new HTTP/2 connection."""

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

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, PingAckReceived, ResponseReceived, StreamEnded, StreamReset


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", type=int)
    parser.add_argument("ping_count", type=int, help="PING frames to send; 0 sends one GET instead")
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.set_alpn_protocols(["h2"])
    connection = H2Connection(config=H2Configuration(client_side=True, header_encoding="utf-8"))
    with socket.create_connection(("127.0.0.1", args.port), timeout=10) as plain:
        with context.wrap_socket(plain, server_hostname="ping.example.com") as tls:
            connection.initiate_connection()
            if args.ping_count:
                for index in range(args.ping_count):
                    connection.ping(index.to_bytes(8, "big"))
            else:
                connection.send_headers(
                    1,
                    [(":method", "GET"), (":scheme", "https"), (":authority", "ping.example.com"), (":path", "/")],
                    end_stream=True,
                )
            tls.sendall(connection.data_to_send())
            acks = 0
            status = None
            while True:
                data = tls.recv(65535)
                if not data:
                    print(f"client_eof ping_acks={acks}", flush=True)
                    return 0
                for event in connection.receive_data(data):
                    if isinstance(event, ConnectionTerminated):
                        print(f"client_goaway error={event.error_code} ping_acks={acks}", flush=True)
                        return 0
                    if isinstance(event, PingAckReceived):
                        acks += 1
                        if acks == args.ping_count:
                            print(f"client_ping_acks={acks}", flush=True)
                            return 0
                    elif isinstance(event, ResponseReceived):
                        status = dict(event.headers)[":status"]
                    elif isinstance(event, StreamReset):
                        print(f"client_stream_reset error={event.error_code}", flush=True)
                        return 0
                    elif isinstance(event, StreamEnded):
                        print(f"client_status={status}", flush=True)
                        return 0
                wire = connection.data_to_send()
                if wire:
                    tls.sendall(wire)


if __name__ == "__main__":
    sys.exit(main())
