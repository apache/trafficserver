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
import time

import pytest

from tools.uranium.services import ATS, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SCRIPT = TEST_DIRECTORY / "dispatch_probe.bt"


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
    pytest.skip("dispatch probe tracing requires root or passwordless sudo")


def wait_for_trace(pattern: str, timeout: float, *, _tracer: ProcessService) -> re.Match[str]:
    """Wait for a line in the live tracer output.

    :param pattern: Expression to find in the accumulated bpftrace output.
    :param timeout: Maximum number of seconds to wait.

    :param _tracer: Test-local tracer configured by the test.
    """

    expression = re.compile(pattern)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if match := expression.search(_tracer.output):
            return match
        if not _tracer.is_running:
            raise AssertionError(f"bpftrace exited before {pattern!r}:\n{_tracer.output}")
        time.sleep(0.1)
    raise AssertionError(f"Expected {pattern!r} in bpftrace output:\n{_tracer.output}")


@pytest.mark.manual(reason="requires privileged bpftrace access")
@pytest.mark.serial
def test_dispatch_probe(ats: ATS, curl: Curl, services: ServiceFactory) -> None:
    """The dispatch probes fire for scheduled events, VIO signals and plugin callbacks, and name their handlers.

    :param ats: Proxy that serves the request through a plugin hook.
    :param curl: Client.
    :param services: Factory that owns the tracer process.
    """

    command = tracer_command()
    ats.records.update({"proxy.config.admin.user_id": "#-1"})
    ats.copy_custom_plugin("{AtsTestPluginsDir}/watchdog_stall.so")
    ats.plugin_config.add_line("watchdog_stall.so")
    tracer = services.process("bpftrace", command)

    ats.start()
    tracer.start()
    wait_for_trace("dispatch_probe: ready", 60, _tracer=tracer)

    curl.run(f"-s -o /dev/null --max-time 10 http://127.0.0.1:{ats.http_port}/")

    for probe in ("event_dispatch", "vio_dispatch"):
        begin = wait_for_trace(rf"{probe}_begin handler=(.*)", 10, _tracer=tracer)
        assert "invoke_handler" in begin.group(1), begin.group(0)
        wait_for_trace(rf"{probe}_end", 10, _tracer=tracer)
    plugin = wait_for_trace(r"plugin_dispatch_begin func=(.*)", 10, _tracer=tracer)
    assert "handle_read_request" in plugin.group(1), plugin.group(0)
    wait_for_trace(r"plugin_dispatch_end", 10, _tracer=tracer)
