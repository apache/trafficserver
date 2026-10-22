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
"""Verify slice background prefetching and its cache-state log."""

import re
import shlex
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

SLICE_PREFETCH_BLOCK_SIZES = (7, 5)

SLICE_PREFETCH_BODY = "lets go surfin now"


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create full and block-range responses keyed by Range.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", lookup_key="{%Range}")
    origin.add_response(
        {"headers": "GET /path HTTP/1.1\r\nHost: origin\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: public, max-age=5\r\n\r\n",
            "body": SLICE_PREFETCH_BODY,
        },
    )
    length = len(SLICE_PREFETCH_BODY)
    for block_size in SLICE_PREFETCH_BLOCK_SIZES:
        for index in range(length // block_size + 1):
            begin = index * block_size
            requested_end = begin + block_size - 1
            end = min(requested_end, length - 1)
            origin.add_response(
                {"headers": ("GET /path HTTP/1.1\r\nHost: *\r\nAccept: */*\r\n"
                             f"Range: bytes={begin}-{requested_end}\r\n\r\n")},
                {
                    "headers":
                        (
                            "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n"
                            "Cache-Control: public, max-age=5\r\n"
                            f"Content-Range: bytes {begin}-{end}/{length}\r\nConnection: close\r\n\r\n"),
                    "body": SLICE_PREFETCH_BODY[begin:end + 1],
                },
            )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure prefetch count one and three mappings plus cache logging.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats")
    required = ("slice.so", "cache_range_requests.so", "xdebug.so")
    if not all(ats.plugin_exists(plugin) for plugin in required):
        pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")
    ats.remap_config.add_lines(
        (
            f"map http://sliceprefetchbytes1/ http://127.0.0.1:{_origin.port} "
            "@plugin=slice.so @pparam=--blockbytes-test=7 @pparam=--prefetch-count=1 "
            "@plugin=cache_range_requests.so",
            f"map http://sliceprefetchbytes2/ http://127.0.0.1:{_origin.port} "
            "@plugin=slice.so @pparam=--blockbytes-test=5 @pparam=--prefetch-count=3 "
            "@plugin=cache_range_requests.so",
        ))
    ats.plugin_config.add_line("xdebug.so --enable=x-cache")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "cache",
                        "format": "%<{Content-Range}psh> %<{X-Cache}psh>"
                    }],
                    "logs": [{
                        "filename": "cache",
                        "format": "cache",
                        "mode": "ascii"
                    }],
                }
        })
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "slice|cache_range_requests|xdebug",
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    return ats


def request(host: str, *, byte_range: str | None = None, _ats: ATS, _curl: Curl) -> str:
    """Request a sliced resource and return headers plus body.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param byte_range: Byte range to request, or whether to send the test range.
    """

    arguments = [
        "--silent",
        "--dump-header",
        "-",
        "--header",
        "x-debug: x-cache",
        "--proxy",
        f"http://127.0.0.1:{_ats.http_port}",
    ]
    if byte_range is not None:
        arguments.extend(("--range", byte_range))
    arguments.append(f"http://{host}/path")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    return result.stdout


def assert_response(output: str, status: str, body: str, cache: str, content_range: str | None = None) -> None:
    """Verify status, assembled body, cache state, and optional range.

    :param output: Output used by this test step.
    :param status: Status used by this test step.
    :param body: HTTP message body.
    :param cache: Cache used by this test step.
    :param content_range: Content range used by this test step.
    """

    assert status in output, output
    assert body in output, output
    assert f"X-Cache: {cache}" in output, output
    if content_range is not None:
        assert f"Content-Range: {content_range}" in output, output


def assert_cache_log(*, _ats: ATS) -> None:
    """Verify foreground and background range cache lookups.

    :param _ats: Test-local ats configured by the test.
    """

    expected = (
        "bytes 0-6/18 miss",
        "bytes 7-13/18 miss",
        "bytes 14-17/18 miss",
        "bytes 14-17/18 hit-fresh",
        "- miss, none",
        "bytes 0-6/18 hit-fresh",
        "bytes 7-13/18 hit-fresh",
        "bytes 14-17/18 hit-fresh",
        "- hit-fresh, none",
        "bytes 0-6/18 hit-stale",
        "bytes 7-13/18 hit-stale",
        "bytes 14-17/18 hit-stale",
        "- hit-stale, none",
        "bytes 0-17/18 hit-fresh, none",
        "bytes 0-4/18 miss",
        "bytes 5-9/18 miss",
        "bytes 10-14/18 miss",
        "bytes 15-17/18 miss",
        "bytes 10-14/18 hit-fresh",
        "bytes 15-17/18 hit-fresh",
        "bytes 5-16/18 miss, none",
        "*/18 hit-fresh, none",
    )
    cache_log = _ats.log_directory / "cache.log"
    content = wait_for_file_lines(cache_log, r"^\*/18 hit-fresh, none$", 1, timeout=15)
    for entry in expected:
        assert re.search(f"^{re.escape(entry)}$", content, re.MULTILINE), entry


def test_slice_prefetch(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Prefetch fills only the configured number of future slice blocks.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_server(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    assert_response(request("sliceprefetchbytes1", _ats=_ats, _curl=_curl), "200 OK", SLICE_PREFETCH_BODY, "miss")
    time.sleep(1)
    assert_response(request("sliceprefetchbytes1", _ats=_ats, _curl=_curl), "200 OK", SLICE_PREFETCH_BODY, "hit-fresh")
    time.sleep(5)
    assert_response(request("sliceprefetchbytes1", _ats=_ats, _curl=_curl), "200 OK", SLICE_PREFETCH_BODY, "hit-stale")
    assert_response(
        request("sliceprefetchbytes1", byte_range="0-", _ats=_ats, _curl=_curl),
        "206 Partial Content",
        SLICE_PREFETCH_BODY,
        "hit-fresh",
        "bytes 0-17/18",
    )
    assert_response(
        request("sliceprefetchbytes2", byte_range="5-16", _ats=_ats, _curl=_curl),
        "206 Partial Content",
        SLICE_PREFETCH_BODY[5:17],
        "miss",
        "bytes 5-16/18",
    )
    invalid = request("sliceprefetchbytes1", byte_range="19-26", _ats=_ats, _curl=_curl)
    assert "416 Requested Range Not Satisfiable" in invalid, invalid
    assert_cache_log(_ats=_ats)
