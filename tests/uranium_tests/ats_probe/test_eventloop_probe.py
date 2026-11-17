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

from pathlib import Path
import os
import re
import shutil
import subprocess
import threading
import time

import pytest

from tools.uranium.services import ATS, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SCRIPT = TEST_DIRECTORY / "eventloop_probe.bt"
STALL_MS = 2000
REPORT = re.compile(r"watchdog_thread_awake pid=(\d+) index=(\d+) tid=(\d+) awake_ms=(\d+)")


def tracer_command() -> tuple[str, ...]:
    """Run bpftrace directly as root or through passwordless sudo."""

    if shutil.which("bpftrace") is None:
        pytest.skip("bpftrace is required")
    if os.geteuid() == 0:
        return ("bpftrace", str(SCRIPT))
    if shutil.which("sudo") is not None:
        result = subprocess.run(("sudo", "-n", "true"), capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return ("sudo", "-n", "bpftrace", str(SCRIPT))
    pytest.skip("event loop probe tracing requires root or passwordless sudo")


def wait_for_trace(pattern: re.Pattern[str], timeout: float, *, _tracer: ProcessService) -> re.Match[str]:
    """Wait for a line in the live tracer output.

    :param pattern: Expression to find in the accumulated bpftrace output.
    :param timeout: Maximum number of seconds to wait.

    :param _tracer: Test-local tracer configured by the test.
    """

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if match := pattern.search(_tracer.output):
            return match
        if not _tracer.is_running:
            raise AssertionError(f"bpftrace exited before {pattern.pattern!r}:\n{_tracer.output}")
        time.sleep(0.1)
    raise AssertionError(f"Expected {pattern.pattern!r} in bpftrace output:\n{_tracer.output}")


@pytest.mark.manual(reason="requires privileged bpftrace access")
@pytest.mark.serial
@pytest.mark.skipif(not os.path.isdir("/proc/self/task"), reason="needs Linux /proc to map thread IDs")
def test_eventloop_probe(ats: ATS, curl: Curl, services: ServiceFactory) -> None:
    """The watchdog probe names the stalled thread, and the event loop probes give its kernel thread ID.

    :param ats: Proxy whose event thread a plugin stalls.
    :param curl: Client.
    :param services: Factory that owns the tracer process.
    """

    command = tracer_command()
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 2,
            "proxy.config.exec_thread.watchdog.timeout_shrink_ms": 200,
            "proxy.config.admin.user_id": "#-1",
        }
    )
    ats.copy_custom_plugin("{AtsTestPluginsDir}/watchdog_stall.so")
    ats.plugin_config.add_line("watchdog_stall.so")
    tracer = services.process("bpftrace", command)

    ats.start()
    tracer.start()
    wait_for_trace(re.compile("eventloop_probe: ready"), 30, _tracer=tracer)
    wait_for_trace(re.compile("eventloop_sleep"), 10, _tracer=tracer)

    stall = threading.Thread(
        target=curl.run, args=(f"-s -o /dev/null --max-time 10 -H 'X-Stall-Ms: {STALL_MS}' http://127.0.0.1:{ats.http_port}/",)
    )
    stall.start()
    report = wait_for_trace(REPORT, 10, _tracer=tracer)
    stall.join()

    pid, index, tid, awake_ms = report.groups()
    assert int(awake_ms) >= 200, report.group(0)
    assert Path(f"/proc/{pid}/task/{tid}/comm").read_text().strip() == f"[ET_NET {index}]", report.group(0)
