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
"""Verify cache_range_requests keys, statuses, and long-key spill handling."""

import re
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory

CACHE_RANGE_REQUESTS_LONG_PATH = "A" * 16400

CACHE_RANGE_REQUESTS_BODY = "lets go surfin now"


def add_response(
    origin: OriginServer,
    uuid: str | None,
    *,
    status: str,
    body: str,
    content_range: str | None = None,
    etag: str = '"path"',
) -> None:
    """Add a UUID-keyed cacheable origin response.

    :param origin: Configured origin service.
    :param uuid: Uuid used by this test step.
    :param status: Status used by this test step.
    :param body: HTTP message body.
    :param content_range: Content range used by this test step.
    :param etag: ETag response header value.
    """

    uuid_line = "" if uuid is None else f"uuid: {uuid}\r\n"
    fields = [f"HTTP/1.1 {status}", "Connection: close"]
    if status.startswith(("200", "206")):
        fields.extend(("Cache-Control: max-age=500", f"Etag: {etag}"))
    if content_range is not None:
        fields.extend(("Accept-Ranges: bytes", f"Content-Range: bytes {content_range}"))
    origin.add_response(
        {"headers": f"GET /path HTTP/1.1\r\nHost: www.example.com\r\n{uuid_line}\r\n"},
        {
            "headers": "\r\n".join(fields) + "\r\n\r\n",
            "body": body
        },
    )


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create full, ranged, parent-keyed, long-key, and 404 responses.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", lookup_key="{%uuid}")
    add_response(origin, "full", status="200 OK", body=CACHE_RANGE_REQUESTS_BODY)
    add_response(origin, "inner", status="206 Partial Content", body=CACHE_RANGE_REQUESTS_BODY[7:15], content_range="7-15/18")
    add_response(origin, "frange", status="206 Partial Content", body=CACHE_RANGE_REQUESTS_BODY, content_range="0-18/18")
    add_response(origin, "last", status="206 Partial Content", body=CACHE_RANGE_REQUESTS_BODY[-5:], content_range="13-18/18")
    add_response(origin, "pselect", status="206 Partial Content", body=CACHE_RANGE_REQUESTS_BODY[1:10], content_range="1-10/19")
    add_response(
        origin,
        "long_key",
        status="206 Partial Content",
        body=CACHE_RANGE_REQUESTS_BODY,
        content_range="0-17/18",
        etag='"longkey"',
    )
    add_response(origin, None, status="404 Not Found", body="Not Found")
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure standard, parent-selection, deprecated, and long-key mappings.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats")
    required = ("cache_range_requests.so", "header_rewrite.so", "xdebug.so")
    if not all(ats.plugin_exists(plugin) for plugin in required):
        pytest.skip("cache_range_requests.so, header_rewrite.so, and xdebug.so are required")
    ats.copy_to_config("reason.conf")
    origin = f"http://127.0.0.1:{_origin.port}"
    ats.remap_config.add_lines(
        (
            f"map http://www.example.com {origin} @plugin=header_rewrite.so "
            f"@pparam={ats.config_directory}/reason.conf @plugin=cache_range_requests.so",
            f"map http://www.longkey.com {origin} @plugin=cache_range_requests.so",
            f"map http://parentselect {origin} @plugin=cache_range_requests.so @pparam=--ps-cachekey",
            f"map http://psd {origin} @plugin=cache_range_requests.so @pparam=ps_mode:cache_key_url",
        ))
    ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-parentselection-key")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "cache_range_requests|http",
    })
    return ats


def request(
        host: str,
        path: str,
        *,
        byte_range: str | None = None,
        uuid: str | None = None,
        debug: str = "x-cache",
        _ats: ATS,
        _curl: Curl) -> CommandResult:
    """Issue a proxied range request and return its complete result.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param path: Resource or file path used by this operation.
    :param byte_range: Byte range to request, or whether to send the test range.
    :param uuid: Uuid used by this test step.
    :param debug: Debug used by this test step.
    """

    arguments = [
        "--silent",
        "--show-error",
        "--dump-header",
        "-",
        "--proxy",
        f"http://127.0.0.1:{_ats.http_port}",
        "--header",
        f"x-debug: {debug}",
    ]
    if byte_range is not None:
        arguments.extend(("--range", byte_range))
    if uuid is not None:
        arguments.extend(("--header", f"uuid: {uuid}"))
    arguments.append(f"http://{host}{path}")
    return _curl.run_for(
        _ats,
        shlex.join(arguments),
    )


def assert_range(result: CommandResult, *, cache: str, content_range: str, body: str) -> None:
    """Verify one successful plugin range response.

    :param result: Completed command result to validate.
    :param cache: Cache used by this test step.
    :param content_range: Content range used by this test step.
    :param body: HTTP message body.
    """

    assert result.returncode == 0, result.output
    assert "206 Foo Bar" in result.stdout, result.output
    assert f"X-Cache: {cache}" in result.stdout, result.output
    assert f"Content-Range: bytes {content_range}" in result.stdout, result.output
    assert body in result.stdout, result.output


def test_cache_range_requests(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Range cache keys remain correct for hits, parent selection, and long URLs.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_server(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    full = request("www.example.com", "/path", uuid="full", _ats=_ats, _curl=_curl)
    assert full.returncode == 0 and CACHE_RANGE_REQUESTS_BODY in full.stdout
    assert_range(
        request("www.example.com", "/path", byte_range="7-15", uuid="inner", _ats=_ats, _curl=_curl),
        cache="miss",
        content_range="7-15/18",
        body=CACHE_RANGE_REQUESTS_BODY[7:15],
    )
    assert_range(
        request("www.example.com", "/path", byte_range="7-15", _ats=_ats, _curl=_curl),
        cache="hit",
        content_range="7-15/18",
        body=CACHE_RANGE_REQUESTS_BODY[7:15],
    )
    assert_range(
        request("www.example.com", "/path", byte_range="0-", uuid="frange", _ats=_ats, _curl=_curl),
        cache="miss",
        content_range="0-18/18",
        body=CACHE_RANGE_REQUESTS_BODY,
    )
    assert_range(
        request("www.example.com", "/path", byte_range="0-", _ats=_ats, _curl=_curl),
        cache="hit",
        content_range="0-18/18",
        body=CACHE_RANGE_REQUESTS_BODY,
    )
    assert_range(
        request("www.example.com", "/path", byte_range="-5", uuid="last", _ats=_ats, _curl=_curl),
        cache="miss",
        content_range="13-18/18",
        body=CACHE_RANGE_REQUESTS_BODY[-5:],
    )
    assert_range(
        request("www.example.com", "/path", byte_range="-5", _ats=_ats, _curl=_curl),
        cache="hit",
        content_range="13-18/18",
        body=CACHE_RANGE_REQUESTS_BODY[-5:],
    )
    for _index in range(2):
        missing = request("www.example.com", "/404", byte_range="0-", _ats=_ats, _curl=_curl)
        assert "404 Not Found" in missing.stdout and "X-Cache: miss" in missing.stdout
    full_range = request("www.example.com", "/path?origin-200", byte_range="7-15", uuid="full", _ats=_ats, _curl=_curl)
    assert "200 OK" in full_range.stdout and "X-Cache: miss" in full_range.stdout
    assert "Content-Range:" not in full_range.stdout
    parent = request(
        "parentselect", "/path", byte_range="1-10", uuid="pselect", debug="x-parentselection-key", _ats=_ats, _curl=_curl)
    assert re.search(r"X-ParentSelection-Key: .*-bytes=", parent.stdout), parent.output
    ordinary = request(
        "www.example.com", "/path", byte_range="7-15", uuid="inner", debug="x-parentselection-key", _ats=_ats, _curl=_curl)
    assert "X-ParentSelection-Key" not in ordinary.stdout
    deprecated = request("psd", "/path", byte_range="1-10", uuid="pselect", debug="x-parentselection-key", _ats=_ats, _curl=_curl)
    assert re.search(r"X-ParentSelection-Key: .*-bytes=", deprecated.stdout), deprecated.output
    long_key = request(
        "www.longkey.com", f"/{CACHE_RANGE_REQUESTS_LONG_PATH}", byte_range="0-17", uuid="long_key", _ats=_ats, _curl=_curl)
    assert long_key.returncode == 0 and "206" in long_key.stdout
    assert "disabling cache for this transaction" not in _ats.diags_log.read_text(errors="replace")
