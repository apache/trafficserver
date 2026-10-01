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

import time
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


def _response(cache_control: str, etag: str) -> dict[str, str]:
    """Cache fill response.

    :param cache_control: Cache-Control response header value.
    :param etag: ETag response header value.
    """
    return {
        "headers": (f"HTTP/1.1 200 OK\r\nCache-Control: {cache_control}\r\nConnection: close\r\n"
                    f"Etag: {etag}\r\n\r\n"),
        "body": "hello hello",
    }


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create cacheable, range, no-store, and global responses.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET /200 HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        _response("max-age=1", "772102f4-56f4bc1e6d417"),
    )
    for path, cache_control, etag in (
        ("range", "max-age=1", "883213f5-67f5bc2e7d528"),
        ("nostore", "nostore", "994324f6-78f6bc3e8d639"),
    ):
        origin.add_response(
            {
                "headers": (f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\n"
                            "Accept: */*\r\nRange: bytes=0-4\r\n\r\n"),
                "body": "",
            },
            _response(cache_control, etag),
        )
    origin.add_response(
        {
            "headers": "GET /global HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        _response("max-age=1", "661091f3-45f3bc0e5d306"),
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Load xdebug and install cache_fill globally and on remap rules.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    missing = [plugin for plugin in ("cache_fill.so", "xdebug.so") if not ats.plugin_exists(plugin)]
    if missing:
        pytest.skip(f"Required plugins are unavailable: {', '.join(missing)}")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "cache_fill|.*cache.*",
    })
    ats.plugin_config.add_lines(("xdebug.so --enable=x-cache,x-cache-key", "cache_fill.so"))
    for path, origin_path in (("200", "200"), ("range", "range"), ("nostore", "nostore"), ("304", "range")):
        ats.remap_config.add_line(
            f"map http://www.example.com/{path} http://127.0.0.1:{_origin.port}/{origin_path} "
            "@plugin=cache_fill.so")
    ats.remap_config.add_line(f"map http://www.example.com/global http://127.0.0.1:{_origin.port}/global")
    return ats


def request(path: str, *, byte_range: bool = False, _ats: ATS, _curl: Curl) -> CommandResult:
    """Fetch one resource with xdebug cache headers enabled.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param byte_range: Byte range to request, or whether to send the test range.
    """

    arguments = [
        "--silent",
        "--dump-header",
        "/dev/stdout",
        "--verbose",
        "--proxy",
        f"localhost:{_ats.http_port}",
        "--header",
        "x-debug: x-cache,x-cache-key",
    ]
    if byte_range:
        arguments.extend(("--range", "0-4"))
    arguments.append(f"http://www.example.com/{path}")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    return result


def require_response(result: CommandResult, cache_status: str, http_status: str) -> None:
    """Require an HTTP status and xdebug cache classification.

    :param result: Completed command result to validate.
    :param cache_status: Expected cache lookup classification.
    :param http_status: Expected HTTP response status.
    """

    assert f"X-Cache: {cache_status}".lower() in result.stdout.lower(), result.output
    assert http_status in result.stdout, result.output


def test_cache_fill(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """cache_fill populates eligible objects and leaves no-store objects alone.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("cache_fill does not support the UDS test transport")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    require_response(request("200", _ats=_ats, _curl=curl), "miss", "200 OK")
    require_response(request("200", _ats=_ats, _curl=curl), "hit-fresh", "200 OK")
    require_response(request("range", byte_range=True, _ats=_ats, _curl=curl), "miss", "200 OK")
    ranged = request("range", byte_range=True, _ats=_ats, _curl=curl)
    require_response(ranged, "hit-fresh", "206 Partial Content")
    assert "Content-Range: bytes 0-4/11".lower() in ranged.stdout.lower()
    require_response(request("nostore", byte_range=True, _ats=_ats, _curl=curl), "miss", "200 OK")
    require_response(request("nostore", byte_range=True, _ats=_ats, _curl=curl), "miss", "200 OK")
    require_response(request("global", _ats=_ats, _curl=curl), "miss", "200 OK")
    time.sleep(0.1)
    require_response(request("global", _ats=_ats, _curl=curl), "hit-fresh", "200 OK")


def test_cache_fill_range_options(ats: ATS, services: ServiceFactory, curl: Curl) -> None:
    """Distinguish positive and negative range-only background-fill decisions.

    :param ats: Isolated server without a global cache_fill instance.
    :param services: Factory for the cacheable origin.
    :param curl: Transport-aware client.
    """
    if curl.uses_uds:
        pytest.skip("cache_fill does not support the UDS test transport")
    if not all(ats.plugin_exists(plugin) for plugin in ("cache_fill.so", "xdebug.so")):
        pytest.skip("cache_fill.so and xdebug.so are required")
    origin = services.origin("range-origin")
    for path, option in (("fill_on_range", "--range-req-only=true"), ("skip_when_plain", "--range-req-only=true"),
                         ("decline_on_range", "--cache-range-req=false")):
        origin.add_response(
            {"headers": f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": f'HTTP/1.1 200 OK\r\nCache-Control: max-age=300\r\nConnection: close\r\nEtag: "{path}"\r\n\r\n',
                "body": "hello hello"
            },
        )
        ats.remap_config.add_line(
            f"map http://www.example.com/{path} http://127.0.0.1:{origin.port}/{path} "
            f"@plugin=cache_fill.so @pparam={option}")
    ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key")
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "cache_fill"})
    origin.start()
    ats.start()

    def request(path: str, byte_range: bool) -> str:
        """Fetch one object with the cache classification in its response.

        :param path: Resource path without the leading slash.
        :param byte_range: Whether to request bytes 0 through 4.
        """
        options = "--range 0-4" if byte_range else ""
        result = curl.run_for(
            ats, f"--silent --dump-header - --proxy localhost:{ats.http_port} "
            f"-H 'x-debug: x-cache,x-cache-key' {options} http://www.example.com/{path}")
        assert result.returncode == 0, result.output
        return result.stdout.lower()

    first = request("fill_on_range", True)
    assert "x-cache: miss" in first and "200 ok" in first, first
    deadline = time.monotonic() + 10
    second = ""
    while time.monotonic() < deadline:
        second = request("fill_on_range", True)
        if "x-cache: hit-fresh" in second:
            break
        time.sleep(0.1)
    assert "x-cache: hit-fresh" in second, second
    assert "206 partial content" in second and "content-range: bytes 0-4/11" in second, second
    for path, ranged in (("skip_when_plain", False), ("decline_on_range", True)):
        response = request(path, ranged)
        assert "x-cache: miss" in response, response
    ats.stop()
    output = ats.traffic_out.read_text(errors="replace")
    for marker in ("_range_req_only=true; This transaction is not a range request",
                   "_cache_range_req=false; This transaction is a range request", "scheduling background fetch"):
        assert marker in output, output
