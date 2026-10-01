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

import pytest
import shlex

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SLICE_BLOCK_SIZE = 10
LARGE_BODY = "large object sliced!"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create small, unsliced-large, and ranged-large responses.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server", lookup_key="{PATH}{%Range}", options={"-v": None})
    origin.add_response(
        {"headers": "GET /small HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
            "body": "smol",
        },
    )
    origin.add_response(
        {"headers": "GET /large HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
            "body": "unsliced large object!",
        },
    )
    for begin in range(0, len(LARGE_BODY), SLICE_BLOCK_SIZE):
        requested_end = begin + SLICE_BLOCK_SIZE - 1
        actual_end = min(begin + SLICE_BLOCK_SIZE, len(LARGE_BODY))
        origin.add_response(
            {"headers": "GET /large HTTP/1.1\r\n"
                        "Host: www.example.com\r\n"
                        f"Range: bytes={begin}-{requested_end}\r\n\r\n"},
            {
                "headers":
                    "HTTP/1.1 206 Partial Content\r\n"
                    "Connection: close\r\n"
                    "Accept-Ranges: bytes\r\n"
                    f"Content-Range: bytes {begin}-{actual_end - 1}/{len(LARGE_BODY)}\r\n"
                    "Cache-Control: max-age=10,public\r\n\r\n",
                "body": LARGE_BODY[begin:actual_end],
            },
        )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure conditional slicing with cache range support.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    required = ("slice.so", "cache_range_requests.so", "xdebug.so")
    missing = [plugin for plugin in required if not ats.plugin_exists(plugin)]
    if missing:
        pytest.skip("Missing plugins: " + ", ".join(missing))
    ats.remap_config.add_line(
        f"map http://slice/ http://127.0.0.1:{_origin.port}/ "
        f"@plugin=slice.so @pparam=--blockbytes-test={SLICE_BLOCK_SIZE} "
        "@pparam=--minimum-size=8 @pparam=--metadata-cache-size=4 "
        "@plugin=cache_range_requests.so")
    ats.plugin_config.add_line("xdebug.so --enable=x-cache")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http|cache|slice|xdebug|cache_range_requests",
        })
    return ats


def request(path: str, *, byte_range: str | None = None, _ats: ATS, _curl: Curl) -> str:
    """Request one object and include response headers in the result.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param byte_range: Byte range to request, or whether to send the test range.
    """

    arguments = [
        "--silent",
        "--include",
        "--proxy",
        f"localhost:{_ats.http_port}",
        "--header",
        "x-debug: x-cache",
    ]
    if byte_range is not None:
        arguments.extend(("--range", byte_range))
    arguments.append(f"http://slice/{path}")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    return result.stdout


def test_slice_conditional(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """slice changes behavior only after an object exceeds the minimum size.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    small_miss = request("small", _ats=_ats, _curl=curl)
    assert "smol" in small_miss and "X-Cache: miss" in small_miss
    small_hit = request("small", _ats=_ats, _curl=curl)
    assert "smol" in small_hit and "X-Cache: hit-fresh" in small_hit
    small_range = request("small", byte_range="1-2", _ats=_ats, _curl=curl)
    assert "mo" in small_range and "X-Cache: hit-fresh" in small_range
    large_unsliced = request("large", _ats=_ats, _curl=curl)
    assert "unsliced large object!" in large_unsliced and "X-Cache: miss" in large_unsliced
    large_sliced = request("large", _ats=_ats, _curl=curl)
    assert LARGE_BODY in large_sliced and "X-Cache: miss" in large_sliced
    large_hit = request("large", _ats=_ats, _curl=curl)
    assert LARGE_BODY in large_hit and "X-Cache: hit-fresh" in large_hit
