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
import re
import signal
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin used to establish a healthy baseline request.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "body": ""},
        {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n",
            "body": "Hello",
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Load the plugin that throws out of a hook and enable the crash-log helper.

    The terminate handler aborts, so ATS is expected to exit on SIGABRT.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", return_code=-signal.SIGABRT, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.proxy_name": "test_proxy",
            "proxy.config.url_remap.remap_required": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "crash_test",
            "proxy.config.crash_log_helper": "traffic_crashlog",
        }
    )
    ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/pluginTest/crash_test/.libs/crash_test.so")
    ats.plugin_config.add_line("crash_test.so")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def verify_healthy_request(*, _ats: ATS, _curl: Curl) -> None:
    """Prove ATS is serving traffic before triggering the fault.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    response = _curl.get(
        _ats,
        headers={"Host": "example.com"},
        options=f"--silent --output /dev/null --write-out '%{{http_code}}'",
    )
    assert response.returncode == 0, response.output
    assert response.stdout == "200"


def trigger_throw(*, _ats: ATS, _curl: Curl) -> None:
    """Send the header that makes crash_test throw a std::runtime_error out of its hook.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    response = _curl.get(
        _ats,
        headers={"Host": "example.com", "X-Crash-Test": "throw"},
        options=f"--silent --output /dev/null",
    )
    assert response.returncode in (52, 56), response.output
    _ats.wait()


def wait_for_crash_log(*, _ats: ATS) -> Path:
    """Wait until traffic_crashlog has finished writing its report.

    :param _ats: Test-local ats configured by the test.
    """

    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        newest = max(_ats.log_directory.glob("crash-*.log"), key=lambda p: p.stat().st_mtime, default=None)
        if newest is not None and "Other Non-Crashing Threads:" in newest.read_text(errors="replace"):
            return newest
        time.sleep(0.1)
    raise AssertionError("traffic_crashlog did not produce a complete crash report")


def test_crash_test_throw(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An uncaught exception on an ET_NET thread is reported before traffic_server aborts.

    Any abort exits with SIGABRT, so the exit status alone cannot show that the terminate handler
    ran. The handler's report of the exception type and message can only come from it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    verify_healthy_request(_ats=_ats, _curl=curl)
    trigger_throw(_ats=_ats, _curl=curl)

    diagnostics = _ats.diags_log.read_text(errors="replace")
    assert "Received throw trigger header - throwing now!" in diagnostics
    assert "This should never be reached." not in diagnostics

    # The handler reports to stderr, which lands in traffic.out. The type name is matched loosely
    # in case the standard library puts it in an inline namespace.
    output = _ats.process_output + _ats.traffic_out.read_text(errors="replace")
    assert re.search(r"traffic_server: terminating on an unhandled std::\S*runtime_error: crash_test throw", output), output

    crash_log = wait_for_crash_log(_ats=_ats).read_text(errors="replace")
    assert "Aborted" in crash_log
    assert "Crashing Thread" in crash_log
    # The terminate handler runs before the stack unwinds, so the frame that threw is still on the
    # crashing thread's stack. The frame must fall between the crashing thread header and the
    # blank line that ends that thread's backtrace.
    assert re.search(r"Crashing Thread[^\n]*:\n(?:[^\n]+\n)*?[^\n]*crash_test_throw\(\)", crash_log), crash_log
