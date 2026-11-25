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
"""Closing listeners on SIGTERM must leave accepted connections working while the proxy drains."""

import signal
import socket

from tools.uranium.services import ATS, wait_for_file_lines

REQUEST = b"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"

# Logged after stop_HttpProxyServer() returns, so the listeners are closed once it appears.
LISTENERS_CLOSED = "received exit signal, shutting down in 3secs"


def _configure(ats: ATS) -> None:
    """Close the listeners on SIGTERM and drain for three seconds.

    :param ats: Proxy to configure before it starts.
    """
    ats.records.update(
        {
            "proxy.config.restart.stop_listening": 1,
            "proxy.config.stop.shutdown_timeout": 3,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "server|http",
        }
    )


def _read_response(conn: socket.socket) -> bytes:
    """Read one HTTP/1 response, using Content-Length when there is one.

    :param conn: Connection to the proxy.
    :returns: The response head and body, or what arrived before the proxy closed.
    """
    conn.settimeout(10)
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(65536)
        if not chunk:
            return data
        data += chunk
    head, _, body = data.partition(b"\r\n\r\n")
    length = None
    for line in head.split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"content-length":
            length = int(value.strip())
    while length is None or len(body) < length:
        chunk = conn.recv(65536)
        if not chunk:
            break
        body += chunk
    return head + b"\r\n\r\n" + body


def test_stop_listening_serves_a_waiting_connection(ats: ATS) -> None:
    """A connection accepted before SIGTERM that sends its request after the listeners close must be answered.

    :param ats: Proxy whose expected exit is explicitly awaited.
    """
    _configure(ats)
    # Without this the kernel holds a connection until its first bytes arrive, so the proxy never sees it idle.
    ats.records.update({"proxy.config.net.defer_accept": 0})
    ats.start()
    with socket.create_connection(("127.0.0.1", ats.http_port), timeout=10) as conn:
        # The protocol probe is waiting for this connection's first bytes.
        wait_for_file_lines(ats.traffic_out, "probe needs data", 1)
        ats.send_signal(signal.SIGTERM)
        wait_for_file_lines(ats.traffic_out, LISTENERS_CLOSED, 1)
        conn.sendall(REQUEST)
        response = _read_response(conn)
    assert response.startswith(b"HTTP/1.1 "), response
    ats.wait(timeout=20)
    assert not ats.is_running


def test_stop_listening_serves_a_keep_alive_request(ats: ATS) -> None:
    """A keep-alive session's next request after the listeners close must be answered.

    :param ats: Proxy whose expected exit is explicitly awaited.
    """
    _configure(ats)
    ats.start()
    with socket.create_connection(("127.0.0.1", ats.http_port), timeout=10) as conn:
        conn.sendall(REQUEST)
        first = _read_response(conn)
        assert first.startswith(b"HTTP/1.1 "), first
        ats.send_signal(signal.SIGTERM)
        wait_for_file_lines(ats.traffic_out, LISTENERS_CLOSED, 1)
        conn.sendall(REQUEST)
        second = _read_response(conn)
    assert second.startswith(b"HTTP/1.1 "), second
    ats.wait(timeout=20)
    assert not ats.is_running
