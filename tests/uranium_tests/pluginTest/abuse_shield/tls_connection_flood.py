#!/usr/bin/env python3
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements. See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership. The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License. You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Flood ATS with TLS connections and report which ones it refuses.

ATS runs TS_VCONN_START when a new connection first becomes readable, not when
it is accepted, so a connection that sends nothing only reaches the plugin when
it closes. Every connection therefore sends a ClientHello once the whole burst
is open. A connection is answered if ATS sends handshake bytes, and refused if
ATS closes it, sends an alert, or stays silent until the deadline.

After the burst, one more connection must be refused. Its source IP is blocked
by then, so it exercises the block table, and the script exits only after ATS
has had the chance to act on every connection.
"""

import argparse
import selectors
import socket
import sys
import time

from tls_client_hello import client_hello


def classify(connection: socket.socket) -> str:
    """Classify a connection whose response is ready to read."""
    try:
        received = connection.recv(4096)
    except (ConnectionResetError, BrokenPipeError):
        return "refused"
    if not received or received[0] == 0x15:
        return "refused"
    if received[0] == 0x16:
        return "answered"
    return "unexpected"


def send_hellos(port: int, count: int, deadline: float) -> dict[str, int]:
    """Open @a count connections, send each a ClientHello, and tally the outcomes."""
    hello = client_hello("example.test")
    connections = [socket.create_connection(("127.0.0.1", port), timeout=5.0) for _ in range(count)]
    for connection in connections:
        try:
            connection.sendall(hello)
        except (ConnectionResetError, BrokenPipeError):
            pass

    results = {"answered": 0, "refused": 0, "unexpected": 0}
    with selectors.DefaultSelector() as selector:
        for connection in connections:
            connection.setblocking(False)
            selector.register(connection, selectors.EVENT_READ)
        pending = len(connections)
        while pending and time.monotonic() < deadline:
            for key, _ in selector.select(timeout=max(0.0, deadline - time.monotonic())):
                connection = key.fileobj
                selector.unregister(connection)
                results[classify(connection)] += 1
                pending -= 1
        # ATS may leave a refused connection open rather than close it.
        results["refused"] += pending

    for connection in connections:
        connection.close()
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--timeout", type=float, default=2.0, help="Seconds to wait for ATS to answer a burst.")
    args = parser.parse_args()

    burst = send_hellos(args.port, args.count, time.monotonic() + args.timeout)
    print("burst: " + " ".join(f"{name}={value}" for name, value in burst.items()))
    if burst["unexpected"]:
        return 1
    if not burst["answered"]:
        print("ATS refused every connection, including those under the rate limit")
        return 1
    if not burst["refused"]:
        print("ATS did not refuse any connection above the rate limit")
        return 1

    probe = send_hellos(args.port, 1, time.monotonic() + args.timeout)
    if probe["refused"] != 1:
        print("ATS answered a connection from the blocked IP")
        return 1
    print("ATS refused a connection from the blocked IP")
    return 0


if __name__ == "__main__":
    sys.exit(main())
