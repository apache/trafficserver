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

from concurrent.futures import ThreadPoolExecutor
import shlex
import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def add_empty_response(origin: OriginServer, host: str) -> None:
    """Configure the microserver response shared by continuation scenarios.

    :param origin: Configured origin service.
    :param host: HTTP host name used for the request.
    """

    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )


def continuation_configure_origin(*, _services: ServiceFactory) -> OriginServer:
    """Create the repeated empty-response origin.

    :param _services: Test-local services configured by the test.
    """

    origin = _services.origin("origin")
    add_empty_response(origin, "continuations.test")
    return origin


def continuation_configure_ats(*, _ats_factory: ATSFactory, _origin: OriginServer, _plugin: str, _protocol: str) -> ATS:
    """Load the accounting plugin and configure HTTP or HTTP/2 ingress.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _plugin: Test-local plugin configured by the test.
    :param _protocol: Test-local protocol configured by the test.
    """

    enable_tls = _protocol == "h2"
    ats = _ats_factory.create("ts", enable_tls=enable_tls, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": _plugin.removesuffix(".so"),
            "proxy.config.cache.enable_read_while_writer": 0,
            **({
                "proxy.config.http2.max_concurrent_streams_in": 65535
            } if enable_tls else {}),
        })
    ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{_plugin}")
    ats.plugin_config.add_line(_plugin)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def continuation_run_clients(*, _ats: ATS, _curl: Curl, _protocol: str, _request_count: int) -> None:
    """Issue independent curl processes concurrently.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _protocol: Test-local protocol configured by the test.
    :param _request_count: Test-local request count configured by the test.
    """

    if _protocol == "h2" and not _curl.supports("http2"):
        pytest.skip("curl HTTP/2 support is required")
    if _protocol == "h2" and _curl.uses_uds:
        pytest.skip("HTTP/2 continuation coverage requires a TCP listener")

    url = (f"https://127.0.0.1:{_ats.https_port}/" if _protocol == "h2" else f"http://127.0.0.1:{_ats.http_port}/")
    options = ["--silent", "--show-error", "--header", "Connection: close"]
    if _protocol == "h2":
        options.extend(["--insecure", "--http2"])

    def request(_index: int) -> None:
        """Request.

        :param _index: Index used by this test step.
        """
        result = _curl.run_for(
            _ats,
            f"{shlex.join(options)} '{url}'",
        )
        assert result.returncode in (0, 2), result.output

    with ThreadPoolExecutor(max_workers=min(32, _request_count)) as executor:
        list(executor.map(request, range(_request_count)))


def continuation_wait_for_metric(name: str, expected: int = 1, *, _ats: ATS) -> None:
    """Wait for a plugin metric to reach @a expected.

    :param _ats: Test-local ats configured by the test.
    :param name: Unique service or case name within this test.
    :param expected: Expected result for this case.
    """

    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        result = _ats.traffic_ctl("metric", "get", name)
        if result.returncode == 0 and result.stdout.rstrip().endswith(f" {expected}"):
            return
        time.sleep(0.1)
    raise AssertionError(f"Metric {name} did not reach {expected}:\n{result.output}")


def continuation_metrics(prefix: str, *, _ats: ATS) -> dict[str, int]:
    """Return all integer metrics under @a prefix.

    :param _ats: Test-local ats configured by the test.
    :param prefix: Prefix used by this test step.
    """

    result = _ats.traffic_ctl("metric", "match", prefix)
    assert result.returncode == 0, result.output
    return {name: int(value) for name, value in re.findall(r"^(\S+)\s+(-?\d+)$", result.stdout, re.MULTILINE)}


def continuation_run_traffic(*, _ats: ATS, _curl: Curl, _origin: OriginServer, _protocol: str, _request_count: int) -> None:
    """Start the topology, issue traffic, and flush plugin metrics.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _protocol: Test-local protocol configured by the test.
    :param _request_count: Test-local request count configured by the test.
    """

    _origin.start()
    _ats.start()
    continuation_run_clients(_ats=_ats, _curl=_curl, _protocol=_protocol, _request_count=_request_count)
    result = _ats.traffic_ctl("plugin", "msg", "done", "done")
    assert result.returncode == 0, result.output


def session_id_configure_ats(*, _ats: ATS, _origin: OriginServer) -> None:
    """Load the session-id verifier and configure the origin mapping.

    :param _ats: Test-local ats configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    _ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "session_id_verify",
            "proxy.config.cache.enable_read_while_writer": 0,
        })
    _ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/continuations/plugins/.libs/session_id_verify.so")
    _ats.plugin_config.add_line("session_id_verify.so")
    _ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")


def session_id_run_requests(options: list[str], url: str, count: int, *, _ats: ATS, _curl: Curl) -> None:
    """Run one protocol's independent client sessions in parallel.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param options: Options used by this test step.
    :param url: Url used by this test step.
    :param count: Count used by this test step.
    """

    def request(_index: int) -> None:
        """Request.

        :param _index: Index used by this test step.
        """
        result = _curl.run_for(
            _ats,
            f"{shlex.join(options)} '{url}'",
        )
        assert result.returncode in (0, 2), result.output

    with ThreadPoolExecutor(max_workers=32) as executor:
        list(executor.map(request, range(count)))


@pytest.mark.parametrize("protocol", ["http1", "h2"])
def test_double_continuation_counts(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    protocol: str,
) -> None:
    """Two continuations observe the same session and transaction hooks.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param protocol: Protocol variant exercised by the test.
    """
    plugin = "continuations_verify.so"
    request_count = 25 if protocol == "h2" else 55
    _origin = continuation_configure_origin(_services=services)
    _ats = continuation_configure_ats(_ats_factory=ats_factory, _origin=_origin, _plugin=plugin, _protocol=protocol)

    continuation_run_traffic(_ats=_ats, _curl=curl, _origin=_origin, _protocol=protocol, _request_count=request_count)
    continuation_wait_for_metric("continuations_verify.test.done", _ats=_ats)
    metrics = continuation_metrics("continuations_verify", _ats=_ats)
    for scope in ("ssn", "txn"):
        assert metrics[f"continuations_verify.{scope}.close.1"] > 0
        assert metrics[f"continuations_verify.{scope}.close.1"] == metrics[f"continuations_verify.{scope}.close.2"]
    assert metrics["continuations_verify.txn.close.1"] == request_count


@pytest.mark.parametrize("protocol", ["http1", "h2"])
def test_continuation_open_close_order(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    protocol: str,
) -> None:
    """Session and transaction hooks open and close in the correct order.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param protocol: Protocol variant exercised by the test.
    """
    plugin = "ssntxnorder_verify.so"
    request_count = 100
    _origin = continuation_configure_origin(_services=services)
    _ats = continuation_configure_ats(_ats_factory=ats_factory, _origin=_origin, _plugin=plugin, _protocol=protocol)

    continuation_run_traffic(_ats=_ats, _curl=curl, _origin=_origin, _protocol=protocol, _request_count=request_count)
    continuation_wait_for_metric("ssntxnorder_verify.test.done", _ats=_ats)
    metrics = continuation_metrics("ssntxnorder_verify", _ats=_ats)
    assert metrics["ssntxnorder_verify.err"] == 0
    for scope in ("ssn", "txn"):
        assert metrics[f"ssntxnorder_verify.{scope}.start"] > 0
        assert metrics[f"ssntxnorder_verify.{scope}.start"] == metrics[f"ssntxnorder_verify.{scope}.close"]
    assert metrics["ssntxnorder_verify.txn.start"] == request_count


def test_session_ids_are_unique(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Session identifiers remain unique across HTTP/1 and HTTP/2.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = services.origin("origin")
    add_empty_response(_origin, "example.com")
    _ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)

    if not curl.supports("http2"):
        pytest.skip("curl HTTP/2 support is required")
    session_id_configure_ats(_ats=_ats, _origin=_origin)
    _origin.start()
    _ats.start()
    count = 100
    session_id_run_requests(
        ["--silent", "--show-error", "--header", "Connection: close"],
        f"http://127.0.0.1:{_ats.http_port}/",
        count,
        _ats=_ats,
        _curl=curl)
    expected = count
    if not curl.uses_uds:
        session_id_run_requests(
            ["--silent", "--show-error", "--insecure", "--http2"],
            f"https://127.0.0.1:{_ats.https_port}/",
            count,
            _ats=_ats,
            _curl=curl)
        expected += count
    session_ids = re.findall(r"session id: ([^\n]+)", _ats.traffic_out.read_text(errors="replace"))
    assert len(session_ids) == expected
    assert len(set(session_ids)) == expected
