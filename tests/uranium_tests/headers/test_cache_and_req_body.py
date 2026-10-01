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

import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the short-lived cacheable response.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers":
                "HTTP/1.1 200 OK\r\n"
                "Connection: close\r\n"
                "Last-Modified: Tue, 08 May 2018 15:49:41 GMT\r\n"
                "Cache-Control: max-age=1\r\n\r\n",
            "body": "xxx",
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Enable cache diagnostics used to distinguish fills and hits.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("xdebug.so"):
        pytest.skip("xdebug.so is not installed")
    ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key,via")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.response_via_str": 3,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def curl_request(*, _ats: ATS, _curl: Curl) -> str:
    """Issue the ordinary request used to fill or hit the cache.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--silent --dump-header - --verbose --ipv4 --http1.1 --header 'x-debug: x-cache,x-cache-key,via' "
            f"--header 'Host: www.example.com' 'http://localhost:{_ats.http_port}/'"),
    )
    assert result.returncode == 0, result.output
    return result.stdout


def raw_request(request: str, *, _ats: ATS) -> str:
    """Send one deliberately body-bearing request with netcat.

    :param _ats: Test-local ats configured by the test.
    :param request: Request used by this test step.
    """

    command = (f"printf %s {shlex.quote(request)} | "
               f"nc 127.0.0.1 -w 1 {_ats.http_port}")
    result = _ats.run_shell(command)
    assert result.returncode == 0, result.output
    return result.stdout


def assert_cached_response(response: str, connection: str) -> None:
    """Verify the framing and cache diagnostics of a cached response.

    :param response: Response used by this test step.
    :param connection: Connection used by this test step.
    """

    lower = response.lower()
    assert "http/1.1 200 ok" in lower
    assert "content-length: 3" in lower
    assert "x-cache: hit-fresh" in lower
    assert f"connection: {connection}" in lower
    assert "xxx" in response


def test_cache_and_req_body(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Cached responses remain correctly framed around body-bearing GET requests.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("raw netcat requests require a TCP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    fill = curl_request(_ats=_ats, _curl=curl)
    assert "X-Cache: miss" in fill
    assert "X-Cache-Key:" in fill
    assert_cached_response(curl_request(_ats=_ats, _curl=curl), "keep-alive")
    hidden_request = (
        "GET / HTTP/1.1\r\n"
        "x-debug: x-cache,x-cache-key,via\r\n"
        "Host: www.example.com\r\n"
        "Content-Length: 71\r\n\r\n"
        "GET /index.html?evil=zorg810 HTTP/1.1\r\n"
        "Host: dummy-host.example.com\r\n\r\n")
    assert_cached_response(raw_request(hidden_request, _ats=_ats), "keep-alive")
    truncated_body = (
        "GET / HTTP/1.1\r\n"
        "x-debug: x-cache,x-cache-key,via\r\n"
        "Host: dummy-host.example.com\r\n"
        "Cache-control: max-age=300\r\n"
        "Content-Length: 100\r\n\r\n"
        "GET /index.html?evil=zorg810 HTTP/1.1\r\n"
        "Host: dummy-host.example.com\r\n\r\n")
    assert_cached_response(raw_request(truncated_body, _ats=_ats), "close")
