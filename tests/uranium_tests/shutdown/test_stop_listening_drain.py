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
"""Closing listeners on SIGTERM must refuse new connections and leave accepted ones working while the proxy drains.

Freeing an acceptor too early is a use-after-free that only an ASAN build reports reliably. On a plain build the
freed memory usually still reads correctly, so the connection tests can pass even with the bug.
"""

import http.client
import signal
import socket
import ssl
import time

import pytest
from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

SHUTDOWN_TIMEOUT = 3

# Logged after stop_HttpProxyServer() returns, so the proxy has stopped accepting once it appears.
LISTENERS_CLOSED = f"received exit signal, shutting down in {SHUTDOWN_TIMEOUT}secs"


def _configure(ats: ATS, debug_tags: str = "server|http") -> None:
    """Close the listeners on SIGTERM, drain for a few seconds, and enable the debug tags the tests wait for.

    :param ats: Proxy to configure before it starts.
    :param debug_tags: Debug tags whose messages the test waits for.
    """
    ats.records.update(
        {
            "proxy.config.restart.stop_listening": 1,
            "proxy.config.stop.shutdown_timeout": SHUTDOWN_TIMEOUT,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": debug_tags,
            # With the default defer_accept, Linux holds a connection in the kernel until its first bytes arrive, so
            # the proxy never sees it waiting.
            "proxy.config.net.defer_accept": 0,
        }
    )


def _close_listeners(ats: ATS) -> None:
    """Send SIGTERM and wait until the proxy has stopped accepting.

    :param ats: Running proxy configured by _configure.
    """
    ats.send_signal(signal.SIGTERM)
    wait_for_file_lines(ats.traffic_out, LISTENERS_CLOSED, 1)


def _assert_served_while_draining(response: http.client.HTTPResponse) -> None:
    """Check that the proxy itself answered the request after the drain began.

    :param response: Response read for a request sent after the listeners closed.
    """
    response.read()
    assert response.status == 404, response.status
    # The proxy adds this only to responses on a draining session.
    assert response.getheader("Connection") == "close", response.getheaders()


def test_stop_listening_serves_a_waiting_connection(ats: ATS) -> None:
    """A connection accepted before SIGTERM that sends its request after the listeners close must be answered.

    :param ats: Proxy whose expected exit is explicitly awaited.
    """
    _configure(ats)
    ats.start()
    conn = http.client.HTTPConnection("127.0.0.1", ats.http_port, timeout=10)
    conn.connect()
    try:
        # The protocol probe is waiting for this connection's first bytes.
        wait_for_file_lines(ats.traffic_out, "probe needs data", 1)
        _close_listeners(ats)
        conn.request("GET", "/", headers={"Host": "example.com"})
        _assert_served_while_draining(conn.getresponse())
    finally:
        conn.close()
    ats.wait(timeout=20)
    assert not ats.is_running


def test_stop_listening_serves_a_keep_alive_request(ats: ATS) -> None:
    """A keep-alive session's next request after the listeners close must be answered.

    :param ats: Proxy whose expected exit is explicitly awaited.
    """
    _configure(ats)
    ats.start()
    conn = http.client.HTTPConnection("127.0.0.1", ats.http_port, timeout=10)
    try:
        conn.request("GET", "/", headers={"Host": "example.com"})
        first = conn.getresponse()
        first.read()
        assert first.status == 404, first.status
        sock = conn.sock
        _close_listeners(ats)
        conn.request("GET", "/", headers={"Host": "example.com"})
        # http.client reconnects on its own if the proxy closed the session, which would not test keep-alive.
        assert conn.sock is sock
        _assert_served_while_draining(conn.getresponse())
    finally:
        conn.close()
    ats.wait(timeout=20)
    assert not ats.is_running


def test_stop_listening_serves_a_waiting_tls_connection(ats_factory: ATSFactory) -> None:
    """A TLS connection accepted before SIGTERM that starts its handshake after the listeners close must be answered.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    _configure(ats, "server|http|ssl")
    ats.start()
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    raw = socket.create_connection(("127.0.0.1", ats.https_port), timeout=10)
    try:
        # The TLS acceptor has handed the connection to its handshake trampoline, which waits for the ClientHello.
        wait_for_file_lines(ats.traffic_out, r"SSLNextProtocolAccept:mainEvent", 1)
        _close_listeners(ats)
        conn = http.client.HTTPSConnection("127.0.0.1", ats.https_port, timeout=10, context=context)
        conn.sock = context.wrap_socket(raw, server_hostname="example.com")
        raw = conn.sock
        conn.request("GET", "/", headers={"Host": "example.com"})
        _assert_served_while_draining(conn.getresponse())
    finally:
        raw.close()
    ats.wait(timeout=20)
    assert not ats.is_running


@pytest.mark.parametrize("accept_threads", [0, 1])
def test_stop_listening_refuses_new_connections(ats: ATS, accept_threads: int) -> None:
    """Once the listeners close on SIGTERM, a new connection must be refused.

    :param ats: Proxy whose expected exit is explicitly awaited.
    :param accept_threads: Value of proxy.config.accept_threads.
    """
    _configure(ats)
    ats.records.update({"proxy.config.accept_threads": accept_threads})
    ats.start()
    _close_listeners(ats)
    # The listeners do not all close at the log line. Fast-accept clones close on other threads a little later, and a
    # dedicated accept thread already blocked in accept() still takes one more connection before it sees the closed
    # socket. So poll until a connection is refused rather than expecting the first attempt to fail.
    deadline = time.monotonic() + 2
    while True:
        try:
            socket.create_connection(("127.0.0.1", ats.http_port), timeout=2).close()
        except ConnectionRefusedError:
            break
        assert time.monotonic() < deadline, "the proxy still accepts connections after the listeners closed"
        time.sleep(0.1)
    ats.wait(timeout=20)
    assert not ats.is_running
