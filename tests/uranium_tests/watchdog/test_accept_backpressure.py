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
"""A second exit signal must not schedule a second graceful shutdown."""
"""Watchdog accept backpressure: shrink, refuse, the loopback exemption, recovery and a clean drain."""
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections.abc import Callable

import pytest

from tools.uranium.services import ATS, Curl

LISTEN_BACKLOG = 128
STALL_MS = 3000

REFUSE_GAUGE = "proxy.process.watchdog.accept_refuse"
REFUSED = "proxy.process.net.watchdog_accepts_refused"
TOTAL_ACCEPTS = "proxy.process.tcp.total_accepts"

pytestmark = pytest.mark.skipif(
    not sys.platform.startswith("linux") or shutil.which("ss") is None,
    reason="needs Linux: 127.0.0.2 as a second loopback address and ss for the accept queue limit",
)


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


def wildcard_queue_limits(port: int) -> list[int]:
    """Accept queue limits (ss Send-Q) of the listeners that are not the loopback companion.

    :param port: Proxy port.
    """
    output = subprocess.run(["ss", "-ltnH", f"sport = :{port}"], capture_output=True, text=True, check=True).stdout
    limits = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 4 and not fields[3].startswith("127."):
            limits.append(int(fields[2]))
    return limits


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
    assert request(curl, "127.0.0.1", port, max_time=5) == (0, "200")
    assert request(curl, "127.0.0.2", port, max_time=5) == (0, "200")
    assert metric(ats, REFUSE_GAUGE) == 0
    assert metric(ats, REFUSED) == 0
    assert set(wildcard_queue_limits(port)) == {LISTEN_BACKLOG}

    # Stall a thread long enough for the monitor to see the span cross the refuse threshold.
    stalled = stall_one_thread(curl, port)
    wait_for(lambda: metric(ats, REFUSE_GAUGE) == 1, 10, "refuse to engage")

    # The wildcard listener is held: the connect is not accepted, so total_accepts does not move and curl times out.
    accepts_before = metric(ats, TOTAL_ACCEPTS)
    code, status = request(curl, "127.0.0.2", port, max_time=1.5)
    assert code == 28 and status != "200", (code, status)
    assert metric(ats, REFUSED) > 0
    assert metric(ats, TOTAL_ACCEPTS) == accepts_before

    # The exempt loopback listener is still served while the wildcard one is held.
    assert request(curl, "127.0.0.1", port, max_time=5) == (0, "200")
    assert metric(ats, TOTAL_ACCEPTS) > accepts_before

    # Shrink has reduced the kernel's queue limit on the held listeners.
    assert max(wildcard_queue_limits(port)) < LISTEN_BACKLOG

    # Recovery: once the stall ends refuse releases, the held address is served again, refusals stop counting, and
    # the queue limit climbs back to its configured depth.
    stalled.join()
    wait_for(lambda: metric(ats, REFUSE_GAUGE) == 0, 15, "refuse to release")
    assert request(curl, "127.0.0.2", port, max_time=5) == (0, "200")

    refused = metric(ats, REFUSED)
    for _ in range(5):
        assert request(curl, "127.0.0.2", port, max_time=5) == (0, "200")
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
    time.sleep(0.5)

    # Every listener is closed now. No acceptor may keep running its retry timer and counting refusals, or touch
    # a descriptor number that the process has since reused.
    refused = metric(ats, REFUSED)
    time.sleep(1.5)
    assert metric(ats, REFUSED) == refused

    stalled.join()
    ats.wait(timeout=20)
    assert not ats.is_running
