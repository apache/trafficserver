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
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve a cacheable object and accept its PURGE request.

    :param services: Factory that owns the origin process.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET /cacheable HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
            "body": ""
        },
        {
            "headers": ("HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=120\r\n"
                        "Content-Length: 5\r\n\r\n"),
            "body": "hello",
        },
    )
    origin.add_response(
        {
            "headers": "PURGE /cacheable HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Keep ATS privileged so bpftrace can observe its cache probes.

    :param ats_factory: Factory that owns the ATS instance.

    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create("ts", enable_cache=True)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|cache",
            "proxy.config.http.cache.required_headers": 0,
            "proxy.config.admin.user_id": "#-1",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def configure_tracer(services: ServiceFactory) -> ProcessService:
    """Create the bpftrace process for the cache probe script.

    :param services: Factory that owns the tracer process.
    """

    return services.process("bpftrace", ("bpftrace", TEST_DIRECTORY / "cache_dir_probe.bt"))


def wait_for_trace(expression: str, timeout: float = 10, *, _tracer: ProcessService) -> None:
    """Wait for one marker in the live tracer output.

    :param expression: Marker to find in the accumulated bpftrace output.
    :param timeout: Maximum number of seconds to wait for the marker.

    :param _tracer: Test-local tracer configured by the test.
    """

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if expression in _tracer.output:
            return
        if not _tracer.is_running and _tracer.output:
            pytest.skip(f"bpftrace cannot attach to the ATS probes:\n{_tracer.output}")
        time.sleep(0.1)
    raise AssertionError(f"Expected {expression!r} in bpftrace output:\n{_tracer.output}")


def request(method: str = "GET", *, _ats: ATS, _curl: Curl) -> None:
    """Send one cache operation through ATS.

    :param method: HTTP method for the cache operation.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        f"--silent --show-error --fail --output /dev/null --request {method} "
        f"--header 'Host: cache-probe.test' http://127.0.0.1:{_ats.http_port}/cacheable",
    )
    assert result.returncode == 0, result.output


def test_cache_dir_probe(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Cache fill and PURGE fire their directory USDT probes.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the origin and tracer processes.
    :param curl: Curl client used for cache operations.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _tracer = configure_tracer(services)

    if os.geteuid() != 0 or shutil.which("bpftrace") is None:
        pytest.skip("cache probe tracing requires root and bpftrace")
    _origin.start()
    _ats.start()
    _tracer.start()
    wait_for_trace("cache_dir_probe: ready", _tracer=_tracer)
    request(_ats=_ats, _curl=curl)
    request("PURGE", _ats=_ats, _curl=curl)
    request(_ats=_ats, _curl=curl)
    wait_for_trace("cache_dir_insert", _tracer=_tracer)
    wait_for_trace("cache_dir_remove", _tracer=_tracer)
