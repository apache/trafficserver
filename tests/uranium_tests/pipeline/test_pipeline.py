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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
IP_ALLOW_CONTENT = """ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods:
      - DELETE
"""


def pipelined_requests_configure_origin(services: ServiceFactory, *, _origin_port: int) -> ProcessService:
    """Start the origin that understands the test's pipelined framing.

    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "pipeline_server.py", "127.0.0.1", str(_origin_port)),
        ready_port=_origin_port,
    )


def pipelined_requests_configure_ats(ats_factory: ATSFactory, *, _buffer_requests: bool, _origin_port: int) -> ATS:
    """Configure reverse proxying and optionally client request buffering.

    :param _buffer_requests: Test-local buffer requests configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin_port}")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|ip_allow",
    })
    if _buffer_requests:
        ats.records.update({"proxy.config.http.request_buffer_enabled": 1})
    ats.write_config_file("ip_allow.yaml", IP_ALLOW_CONTENT)
    return ats


def pipelined_requests_configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Send the three purpose-built requests on one connection.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "pipeline_client.py",
            "127.0.0.1",
            str(_ats.http_port),
            "server.com",
            "server.com",
        ),
    )


def pipelined_requests_verify(result: CommandResult, *, _origin: ProcessService) -> None:
    """Require two origin responses and one ATS-generated denial.

    :param _origin: Test-local origin configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert "X-Response: first" in result.output
    assert "X-Response: second" in result.output
    assert "X-Response: third" not in result.output
    assert "403" in result.output
    assert "/first" in _origin.output
    assert "/second" in _origin.output
    assert "/third" not in _origin.output


def request_framing_configure_origin(services: ServiceFactory, *, _origin_port: int) -> ProcessService:
    """Start the origin that records exact request boundaries.

    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "request_framing_server.py", "127.0.0.1", str(_origin_port)),
        ready_port=_origin_port,
    )


def request_framing_configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Configure a cache-free reverse proxy to the recording origin.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin_port}/")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http",
    })
    return ats


def request_framing_configure_client(services: ServiceFactory, *, _ats: ATS, _mode: str) -> ProcessService:
    """Send the selected request shape over a raw socket.

    :param _ats: Test-local ats configured by the test.
    :param _mode: Test-local mode configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "request_framing_client.py",
            "127.0.0.1",
            str(_ats.http_port),
            "www.example.com",
            _mode,
        ),
    )


def request_framing_verify(result: CommandResult, *, _mode: str, _origin: ProcessService) -> None:
    """Verify client responses and the request boundaries seen by the origin.

    :param _mode: Test-local mode configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    origin_output = _origin.output
    if _mode == "pipeline":
        assert "STATUS_LINE_COUNT: 2" in result.output
        assert "X-Origin-Response: first" in result.output
        assert "X-Origin-Response: second" in result.output
        assert "REQUEST_LINE: POST / HTTP/1.1" in origin_output
        assert "REQUEST_LINE: GET /second HTTP/1.1" in origin_output
        assert "ORIGIN_REQUEST_COUNT: 2" in origin_output
        assert "ORIGIN_REQUEST_COUNT: 3" not in origin_output
        assert re.search(r"BODY:.*GET /second", origin_output) is None
        assert re.search(r"BODY:.*X-Marker", origin_output) is None
    else:
        assert "HTTP/1.1 400" in result.output
        assert "STATUS_LINE_COUNT: 1" in result.output
        assert "X-Origin-Response: second" not in result.output
        assert "REQUEST_LINE:" not in origin_output


@pytest.mark.parametrize("buffer_requests", (False, True), ids=("streaming", "buffered"))
def test_pipelined_requests(buffer_requests: bool, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS handles pipelined chunked requests with and without buffering.

    :param buffer_requests: Buffer requests used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _origin = pipelined_requests_configure_origin(services, _origin_port=_origin_port)
    _ats = pipelined_requests_configure_ats(ats_factory, _buffer_requests=buffer_requests, _origin_port=_origin_port)
    _client = pipelined_requests_configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=10)
    _origin.wait(timeout=5)
    pipelined_requests_verify(result, _origin=_origin)


@pytest.mark.parametrize("mode", ("pipeline", "conflicting_cl"))
def test_request_framing(mode: str, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS preserves valid framing and rejects conflicting Content-Length fields.

    :param mode: Mode used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _origin = request_framing_configure_origin(services, _origin_port=_origin_port)
    _ats = request_framing_configure_ats(ats_factory, _origin_port=_origin_port)
    _client = request_framing_configure_client(services, _ats=_ats, _mode=mode)

    _origin.start()
    _ats.start()
    request_framing_verify(_client.run(timeout=20), _mode=mode, _origin=_origin)
