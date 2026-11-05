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
"""Watchdog accept backpressure: shrink, refuse, the loopback exemption, recovery and a clean drain."""
import shlex
import signal
import socket
import struct
import sys
import threading
import time
from collections.abc import Callable

import pytest

from tools.uranium.services import ATS, Curl

LISTEN_BACKLOG = 128
STALL_MS = 6000

REFUSE_GAUGE = "proxy.process.watchdog.accept_refuse"
REFUSED = "proxy.process.net.watchdog_accepts_refused"
TOTAL_ACCEPTS = "proxy.process.tcp.total_accepts"

# What watchdog_stall.so answers with; see the plugin for why it is not a 200.
SERVED = "418"

pytestmark = pytest.mark.skipif(
    not sys.platform.startswith("linux"),
    reason="needs Linux: 127.0.0.2 as a second loopback address and sock_diag for the accept queue limit",
)

SOCK_DIAG_BY_FAMILY = 20
NLM_F_REQUEST_DUMP = 0x1 | 0x300
NLMSG_DONE = 3
NLMSG_ERROR = 2
TCP_LISTEN_MASK = 1 << 10


def metric(ats: ATS, name: str) -> int:
    """Read one metric through traffic_ctl.

    :param ats: Running proxy.
    :param name: Metric name.
    """
    result = ats.traffic_ctl("metric", "get", name)
    assert result.returncode == 0, result.output
    return int(result.stdout.split()[-1])


def wait_for(condition: Callable[[], bool], timeout: float, description: str) -> None:
    """Poll until the condition holds.

    :param condition: Predicate polled every 100 ms.
    :param timeout: Seconds to wait.
    :param description: What was being waited for, for the failure message.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.1)
    raise AssertionError(f"timed out after {timeout:g}s waiting for {description}")


def request(curl: Curl, address: str, port: int, *, max_time: float, stall_ms: int = 0) -> tuple[int, str]:
    """Send one request straight to an address, bypassing the selected transport.

    :param curl: Client.
    :param address: Loopback address to connect to.
    :param port: Proxy port.
    :param max_time: Seconds curl waits before giving up.
    :param stall_ms: Milliseconds the plugin should block the serving thread for.
    :return: curl's exit status and the response status it printed.
    """
    header = f"-H {shlex.quote(f'X-Stall-Ms: {stall_ms}')}" if stall_ms else ""
    result = curl.run(f"-s -o /dev/null -w %{{http_code}} --max-time {max_time} {header} http://{address}:{port}/")
    return result.returncode, result.stdout.strip()


def listener_queue_limits(port: int, *, loopback: bool = False) -> list[int]:
    """Accept queue limits of the TCP listeners on a port, read from the kernel's sock_diag interface.

    This is the figure ss prints as Send-Q for a listener. It is read directly so the test does not need iproute.

    :param port: Proxy port.
    :param loopback: Whether to report the 127.0.0.0/8 companion listener instead of the wildcard ones.
    :return: One limit per matching listener.
    """
    limits = []
    for family in (socket.AF_INET, socket.AF_INET6):
        with socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, 4) as nl:  # NETLINK_SOCK_DIAG
            request = struct.pack("=BBBBI", family, socket.IPPROTO_TCP, 0, 0, TCP_LISTEN_MASK) + bytes(48)
            nl.send(struct.pack("=IHHII", 16 + len(request), SOCK_DIAG_BY_FAMILY, NLM_F_REQUEST_DUMP, 1, 0) + request)
            done = False
            while not done:
                data = nl.recv(65536)
                offset = 0
                while offset + 16 <= len(data):
                    length, kind = struct.unpack_from("=IH", data, offset)
                    if kind in (NLMSG_DONE, NLMSG_ERROR):
                        done = True
                        break
                    body = offset + 16
                    sport = struct.unpack_from("!H", data, body + 4)[0]
                    is_loopback = family == socket.AF_INET and data[body + 8] == 127
                    if sport == port and is_loopback == loopback:
                        limits.append(struct.unpack_from("=I", data, body + 60)[0])
                    offset += (length + 3) & ~3
    return limits


def wildcard_queue_limits(port: int) -> list[int]:
    """Accept queue limits of the listeners that are not the loopback companion.

    :param port: Proxy port.
    """
    return listener_queue_limits(port)


def open_held_connection(address: str, port: int) -> socket.socket:
    """Connect and send a request, returning the socket so the caller can see whether it is ever answered.

    The kernel completes the handshake from its own accept queue, so this succeeds even while the proxy is not accepting.

    :param address: Loopback address to connect to.
    :param port: Proxy port.
    :return: The connected socket, with its request already written.
    """
    conn = socket.create_connection((address, port), timeout=3)
    conn.sendall(f"GET / HTTP/1.1\r\nHost: {address}:{port}\r\nConnection: close\r\n\r\n".encode())
    return conn


def stall_one_thread(curl: Curl, port: int) -> threading.Thread:
    """Block one event thread by sending it a request the plugin stalls.

    :param curl: Client.
    :param port: Proxy port.
    :return: The already started thread that is holding the request.
    """
    thread = threading.Thread(target=request, args=(curl, "127.0.0.1", port), kwargs={"max_time": 15, "stall_ms": STALL_MS})
    thread.start()
    return thread


@pytest.mark.parametrize("accept_threads", [0, 1])
def test_accept_backpressure(ats: ATS, curl: Curl, accept_threads: int) -> None:
    """Drive the watchdog through a stall, recovery and a SIGTERM drain.

    :param ats: Proxy to stall.
    :param curl: Client.
    :param accept_threads: Value of proxy.config.accept_threads.
    """
    ats.records.update(
        {
            "proxy.config.accept_threads": accept_threads,
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
            "proxy.config.exec_thread.watchdog.mode": 1,
            "proxy.config.exec_thread.watchdog.timeout_shrink_ms": 400,
            "proxy.config.exec_thread.watchdog.timeout_refuse_ms": 800,
            "proxy.config.net.listen_backlog": LISTEN_BACKLOG,
            "proxy.config.restart.stop_listening": 1,
            "proxy.config.stop.shutdown_timeout": 10,
        })
    ats.copy_custom_plugin("{AtsTestPluginsDir}/watchdog_stall.so")
    ats.plugin_config.add_line("watchdog_stall.so")
    ats.start()
    port = ats.http_port

    # Healthy: both addresses are served, nothing is refused, and the queue is at its configured depth.
    assert request(curl, "127.0.0.1", port, max_time=5) == (0, SERVED)
    assert request(curl, "127.0.0.2", port, max_time=5) == (0, SERVED)
    assert metric(ats, REFUSE_GAUGE) == 0
    assert metric(ats, REFUSED) == 0
    assert set(wildcard_queue_limits(port)) == {LISTEN_BACKLOG}

    # Stall a thread long enough for the monitor to see the span cross the refuse threshold.
    stalled = stall_one_thread(curl, port)
    wait_for(lambda: metric(ats, REFUSE_GAUGE) == 1, 10, "refuse to engage")

    # The wildcard listener is held: a connection already sitting in its queue is not answered while refuse is engaged.
    held = open_held_connection("127.0.0.2", port)
    held.settimeout(1)
    with pytest.raises(socket.timeout):
        held.recv(1)

    # A new connect is not accepted either, so total_accepts does not move and curl times out.
    accepts_before = metric(ats, TOTAL_ACCEPTS)
    code, status = request(curl, "127.0.0.2", port, max_time=1.5)
    assert code == 28 and status != SERVED, (code, status)
    assert metric(ats, REFUSED) > 0
    assert metric(ats, TOTAL_ACCEPTS) == accepts_before

    # The exempt loopback listener is still served while the wildcard one is held.
    assert request(curl, "127.0.0.1", port, max_time=5) == (0, SERVED)
    assert metric(ats, TOTAL_ACCEPTS) > accepts_before

    # Shrink has reduced the kernel's queue limit on the held listeners.
    assert max(wildcard_queue_limits(port)) < LISTEN_BACKLOG

    # Recovery: once the stall ends refuse releases, the held address is served again, refusals stop counting, and
    # the queue limit climbs back to its configured depth.
    stalled.join()
    wait_for(lambda: metric(ats, REFUSE_GAUGE) == 0, 15, "refuse to release")

    # The connection that waited out the stall is served rather than reset: a held client sees no error.
    held.settimeout(10)
    assert held.recv(64).startswith(f"HTTP/1.1 {SERVED}".encode())
    held.close()
    assert request(curl, "127.0.0.2", port, max_time=5) == (0, SERVED)

    refused = metric(ats, REFUSED)
    for _ in range(5):
        assert request(curl, "127.0.0.2", port, max_time=5) == (0, SERVED)
    assert metric(ats, REFUSED) == refused

    def queue_restored() -> bool:
        request(curl, "127.0.0.2", port, max_time=5)
        return set(wildcard_queue_limits(port)) == {LISTEN_BACKLOG}

    wait_for(queue_restored, 30, "the accept queue limit to return to its configured depth")

    # Drain while refusing: stall again, and signal once refuse has engaged and the held listener has been poked.
    stalled = stall_one_thread(curl, port)
    wait_for(lambda: metric(ats, REFUSE_GAUGE) == 1, 10, "refuse to engage again")
    request(curl, "127.0.0.2", port, max_time=1)
    ats.send_signal(signal.SIGTERM)
    wait_for(
        lambda: not listener_queue_limits(port) and not listener_queue_limits(port, loopback=True), 10, "every listener to close")

    # No acceptor may keep running its retry timer and counting refusals, or touch a descriptor number that the process
    # has since reused. Several retry periods pass in the window, so a surviving timer would show.
    refused = metric(ats, REFUSED)
    time.sleep(1)
    assert metric(ats, REFUSED) == refused

    stalled.join()
    ats.wait(timeout=20)
    assert not ats.is_running
