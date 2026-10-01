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
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Enable detailed transform logging and bounded transaction timeouts.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("xdebug.so"):
        pytest.skip("xdebug.so is required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "xdebug_transform",
            "proxy.config.http.transaction_no_activity_timeout_in": 10,
            "proxy.config.http.transaction_no_activity_timeout_out": 10,
        })
    ats.plugin_config.add_line("xdebug.so --enable=probe-full-json")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin_port}/")
    return ats


def configure_server(services: ServiceFactory, *, _ats: ATS, _origin_port: int, _origin_ready: Path) -> ProcessService:
    """Run the netcat origin that pauses between its two chunks.

    :param _ats: Test-local ats configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param _origin_ready: Test-local origin ready configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    if shutil.which("nc") is None:
        pytest.skip("nc is required")
    request_path = _ats.run_directory / "server_request.txt"
    return services.process(
        "slow-origin",
        (
            "bash",
            str(TEST_DIRECTORY / "slow-body-server.sh"),
            str(_origin_port),
            str(request_path),
            str(_origin_ready),
        ),
    )


def wait_for_origin(*, _origin_ready: Path) -> None:
    """Wait until the server has opened both ends of its response pipe.

    :param _origin_ready: Test-local origin ready configured by the test.
    """

    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if _origin_ready.exists():
            return
        time.sleep(0.05)
    raise AssertionError("The slow origin did not open its listener")


def request(*, _ats: ATS, _curl: Curl) -> None:
    """Require the transformed request to complete before curl's hang detector.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.get(
        _ats,
        "/test",
        headers={
            "Host": "example.com",
            "X-Debug": "probe-full-json=nobody"
        },
        options=f"--silent --output /dev/null --write-out '%{{http_code}}' --max-time 8",
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert result.stdout == "200"


def verify_transform_progress(*, _ats: ATS) -> None:
    """Bound empty callbacks and require both body chunks to be consumed.

    :param _ats: Test-local ats configured by the test.
    """

    trace = _ats.traffic_out.read_text(errors="replace")
    expected_count = trace.count("bytes of body is expected")
    consumed_count = len([line for line in trace.splitlines() if "consumed" in line and "bytes" in line])
    assert expected_count <= 10, f"The transform appears to be looping ({expected_count} callbacks)\n{trace}"
    assert consumed_count == 2, f"Expected both delayed chunks to be consumed, found {consumed_count}\n{trace}"


def test_x_probe_full_json_slow_origin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A delayed chunked origin cannot send the full-JSON transform into a callback loop.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin_port = services.allocate_port()
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)
    _origin_ready = _ats.run_directory / "origin.ready"
    _server = configure_server(services, _ats=_ats, _origin_port=_origin_port, _origin_ready=_origin_ready)

    _ats.start()
    _server.start()
    wait_for_origin(_origin_ready=_origin_ready)
    request(_ats=_ats, _curl=curl)
    _server.stop()
    verify_transform_progress(_ats=_ats)
