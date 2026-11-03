"""Client connections for test_connections_throttle_exempt.py."""
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

from collections.abc import Callable
import socket
import ssl
import time

TIMEOUT = 10.0
POLL_INTERVAL = 0.1


def proxy_header(source: str, port: int) -> bytes:
    """Return a PROXY protocol v1 header that claims the connection comes from @a source.

    :param source: The claimed source address.
    :param port: The destination port.
    """

    if ":" in source:
        return f"PROXY TCP6 {source} ::1 40000 {port}\r\n".encode()
    return f"PROXY TCP4 {source} 127.0.0.1 40000 {port}\r\n".encode()


def connect(host: str, port: int, tls: bool = False, proxy_source: str = "") -> socket.socket:
    """Open a connection to @a host, an IPv4 or IPv6 address.

    :param tls: Use TLS.
    :param proxy_source: Start with a PROXY protocol header that claims this source address.
    """

    sock = socket.create_connection((host, port), timeout=TIMEOUT)
    if proxy_source:
        sock.sendall(proxy_header(proxy_source, port))
    if tls:
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        sock = context.wrap_socket(sock, server_hostname="healthcheck")
    return sock


def exchange(sock: socket.socket, close: bool) -> str:
    """Send a health check request on @a sock and return the response status line.

    :param close: Ask the proxy to close the connection after the response.
    :return: The status line, or an empty string if the proxy closed the connection first.
    """

    request = b"GET / HTTP/1.1\r\nHost: healthcheck\r\n"
    if close:
        request += b"Connection: close\r\n"
    try:
        sock.sendall(request + b"\r\n")
        response = b""
        while b"\r\n\r\n" not in response:
            data = sock.recv(4096)
            if not data:
                return ""
            response += data
    except (ConnectionResetError, BrokenPipeError, ssl.SSLError):
        return ""

    header, _, body = response.partition(b"\r\n\r\n")
    lines = header.decode().split("\r\n")
    length = 0
    for line in lines[1:]:
        name, _, value = line.partition(":")
        if name.strip().lower() == "content-length":
            length = int(value)
    while len(body) < length:
        data = sock.recv(4096)
        if not data:
            break
        body += data
    return lines[0]


def wait_for_close(sock: socket.socket) -> None:
    """Return once the proxy has closed the TCP connection under @a sock."""

    if isinstance(sock, ssl.SSLSocket):
        # The TLS close_notify comes before the socket is closed, so read on to the TCP FIN.
        try:
            sock = sock.unwrap()
        except (OSError, ssl.SSLError):
            return
    try:
        while sock.recv(4096):
            pass
    except ConnectionResetError:
        pass


def attempt(host: str, port: int, tls: bool = False, proxy_source: str = "", while_open: Callable[[], None] | None = None) -> str:
    """Make one health check request and return whether it was served or refused.

    :param tls: Use TLS.
    :param proxy_source: Start with a PROXY protocol header that claims this source address.
    :param while_open: Call this once the request is served, then ask the proxy to close the connection and wait until it
        does.
    :return: 'served' or 'refused'.
    """

    try:
        sock = connect(host, port, tls, proxy_source)
    except (ConnectionResetError, BrokenPipeError, ssl.SSLError):
        # A TLS handshake or the PROXY header fails if the proxy closes the connection at accept.
        return "refused"
    with sock:
        status = exchange(sock, close=while_open is None)
        if not status:
            return "refused"
        assert status == "HTTP/1.1 200 OK", f"The health check from {host} got {status}"
        if while_open is not None:
            while_open()
            exchange(sock, close=True)
            wait_for_close(sock)
        return "served"


def request(host: str, port: int, expect: str, retry: float = 0, tls: bool = False, proxy_source: str = "") -> None:
    """Make health check requests until the outcome is @a expect, or until @a retry seconds pass.

    :param expect: 'served' or 'refused'.
    :param retry: How many seconds to retry for.
    :param tls: Use TLS.
    :param proxy_source: Start with a PROXY protocol header that claims this source address.
    """

    deadline = time.monotonic() + retry
    outcome = attempt(host, port, tls, proxy_source)
    while outcome != expect and time.monotonic() < deadline:
        time.sleep(POLL_INTERVAL)
        outcome = attempt(host, port, tls, proxy_source)
    assert outcome == expect, f"A connection from {host} to port {port} was {outcome}, not {expect}"


def hold(host: str, port: int, count: int) -> list[socket.socket]:
    """Open @a count connections that each send part of a request, so that the proxy keeps them open."""

    socks = [connect(host, port) for _ in range(count)]
    for sock in socks:
        sock.sendall(b"GET / HTTP/1.1\r\nHost: held\r\n")
    return socks
