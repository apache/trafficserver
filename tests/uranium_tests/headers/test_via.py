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
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Load the microserver hook that records normalized Via headers.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin(
        "server",
        options={"--load": TEST_DIRECTORY / "via-observer.py"},
    )
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _curl: Curl, _enable_quic: bool, _origin: OriginServer) -> ATS:
    """Enable maximal Via detail on every supported listener.

    :param _curl: Test-local curl configured by the test.
    :param _enable_quic: Test-local enable quic configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_quic=_enable_quic)
    records: dict[str, object] = {
        "proxy.config.http.insert_request_via_str": 4,
        "proxy.config.http.insert_response_via_str": 4,
    }
    if not _curl.uses_uds:
        server_ports = (f"{ats.http_port} {ats.ipv6_port}:ipv6 "
                        f"{ats.https_port}:ssl {ats.ipv6_https_port}:ssl:ipv6")
        if _enable_quic:
            server_ports += f" {ats.https_port}:quic {ats.ipv6_https_port}:quic:ipv6"
        records["proxy.config.http.server_ports"] = server_ports
    ats.records.update(records)
    ats.remap_config.add_lines(
        (
            f"map http://www.example.com http://127.0.0.1:{_origin.port}",
            f"map https://www.example.com http://127.0.0.1:{_origin.port}",
        ))
    return ats


def __curl(*arguments: str, _ats: ATS, _curl: Curl) -> None:
    """Run one client protocol variant.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _curl.run_for(
        _ats,
        f"--verbose {shlex.join(arguments)}",
    )
    assert result.returncode == 0, result.output


def run_uds_requests(*, _ats: ATS, _curl: Curl) -> int:
    """Exercise the Via stack available over a Unix socket.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    __curl("--http1.1", "--proxy", f"localhost:{_ats.http_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    __curl("--http1.0", "--proxy", f"localhost:{_ats.http_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    __curl("--http1.1", "--proxy", f"localhost:{_ats.ipv6_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    return 3


def run_network_requests(*, _ats: ATS, _curl: Curl, _enable_quic: bool) -> int:
    """Exercise clear-text, TLS, HTTP/2, optional HTTP/3, and IPv6.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _enable_quic: Test-local enable quic configured by the test.
    """

    __curl("--ipv4", "--http1.1", "--proxy", f"localhost:{_ats.http_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    __curl("--ipv4", "--http1.0", "--proxy", f"localhost:{_ats.http_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    __curl(
        "--ipv4",
        "--http2",
        "--insecure",
        "--header",
        "Host: www.example.com",
        f"https://localhost:{_ats.https_port}",
        _ats=_ats,
        _curl=_curl)
    count = 3
    if _enable_quic:
        __curl(
            "--ipv4",
            "--http3",
            "--insecure",
            "--header",
            "Host: www.example.com",
            f"https://localhost:{_ats.https_port}",
            _ats=_ats,
            _curl=_curl)
        count += 1
    __curl(
        "--ipv4",
        "--http1.1",
        "--insecure",
        "--header",
        "Host: www.example.com",
        f"https://localhost:{_ats.https_port}",
        _ats=_ats,
        _curl=_curl)
    __curl("--ipv6", "--http1.1", "--proxy", f"localhost:{_ats.ipv6_port}", "http://www.example.com", _ats=_ats, _curl=_curl)
    __curl(
        "--ipv6",
        "--http1.1",
        "--insecure",
        "--header",
        "Host: www.example.com",
        f"https://localhost:{_ats.ipv6_https_port}",
        _ats=_ats,
        _curl=_curl)
    return count + 3


def test_via(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Upstream Via headers accurately describe each client protocol stack.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if not Curl.supports("http2") or not Curl.supports("IPv6"):
        pytest.skip("curl HTTP/2 and IPv6 support are required")
    _enable_quic = ats_factory.has_feature("TS_USE_QUIC") and Curl.supports("http3")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _curl=curl, _enable_quic=_enable_quic, _origin=_origin)

    _origin.start()
    _ats.start()
    count = run_uds_requests(
        _ats=_ats, _curl=curl) if curl.uses_uds else run_network_requests(
            _ats=_ats, _curl=curl, _enable_quic=_enable_quic)
    log_path = _origin.run_directory / "via.log"
    wait_for_file_lines(log_path, r"^Via:", count)
    gold = "via_uds.gold" if curl.uses_uds else ("via_h3.gold" if _enable_quic else "via.gold")
    assert_matches_gold(log_path.read_text(errors="replace"), TEST_DIRECTORY / gold)
