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
"""Verify cache_range_requests identity headers control freshness."""

import time
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

CACHE_RANGE_IDENT_LAST_MODIFIED = "Fri, 07 Mar 2025 18:06:58 GMT"
CACHE_RANGE_IDENT_ETAG = '"772102f4-56f4bc1e6d417"'

CACHE_RANGE_IDENT_BODY = "lets go surfin now"


def add_asset(
    origin: OriginServer,
    path: str,
    *,
    etag: str | None,
    last_modified: str | None,
    max_age: int,
) -> None:
    """Add one cacheable full-range response.

    :param origin: Configured origin service.
    :param path: Resource or file path used by this operation.
    :param etag: ETag response header value.
    :param last_modified: Last modified used by this test step.
    :param max_age: Max age used by this test step.
    """

    fields = [
        "HTTP/1.1 206 Partial Content",
        "Accept-Ranges: bytes",
        f"Cache-Control: max-age={max_age}",
        f"Content-Range: bytes 0-{len(CACHE_RANGE_IDENT_BODY)}/{len(CACHE_RANGE_IDENT_BODY)}",
        "Connection: close",
    ]
    if etag is not None:
        fields.append(f"Etag: {etag}")
    if last_modified is not None:
        fields.append(f"Last-Modified: {last_modified}")
    origin.add_response(
        {"headers": (f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\nAccept: */*\r\nRange: bytes=0-\r\n\r\n")},
        {
            "headers": "\r\n".join(fields) + "\r\n\r\n",
            "body": CACHE_RANGE_IDENT_BODY
        },
    )


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create short- and long-lived identity combinations.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    add_asset(origin, "both", etag=CACHE_RANGE_IDENT_ETAG, last_modified=CACHE_RANGE_IDENT_LAST_MODIFIED, max_age=1)
    add_asset(origin, "etag", etag=CACHE_RANGE_IDENT_ETAG, last_modified=None, max_age=1)
    add_asset(origin, "lm", etag=None, last_modified=CACHE_RANGE_IDENT_LAST_MODIFIED, max_age=1)
    add_asset(origin, "custom", etag="foo", last_modified=None, max_age=1)
    add_asset(origin, "fresh", etag="fresh", last_modified=CACHE_RANGE_IDENT_LAST_MODIFIED, max_age=3600)
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure standard and custom identity-header mappings.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats")
    if not ats.plugin_exists("cache_range_requests.so") or not ats.plugin_exists("xdebug.so"):
        pytest.skip("cache_range_requests.so and xdebug.so are required")
    ats.remap_config.add_lines(
        (
            f"map http://ident http://127.0.0.1:{_origin.port} "
            "@plugin=cache_range_requests.so @pparam=--consider-ident",
            f"map http://identheader http://127.0.0.1:{_origin.port} "
            "@plugin=cache_range_requests.so @pparam=--consider-ident @pparam=--ident-header=CrrIdent",
        ))
    ats.plugin_config.add_line("xdebug.so --enable=x-cache")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "cache_range_requests",
    })
    return ats


def request(host: str, path: str, expected_cache: str, ident: str | None = None, *, _ats: ATS, _curl: Curl) -> None:
    """Issue a full-range request and verify its x-cache state.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param path: Resource or file path used by this operation.
    :param expected_cache: Expected cache for this case.
    :param ident: Ident used by this test step.
    """

    arguments = [
        "--silent",
        "--dump-header",
        "-",
        "--output",
        "/dev/null",
        "--proxy",
        f"http://127.0.0.1:{_ats.http_port}",
        "--header",
        "x-debug: x-cache",
        "--range",
        "0-",
    ]
    if ident is not None:
        header = "CrrIdent" if host == "identheader" else "X-Crr-Ident"
        arguments.extend(("--header", f"{header}: {ident}"))
    arguments.append(f"http://{host}/{path}")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    assert f"X-Cache: {expected_cache}" in result.stdout, result.output


def test_cache_range_requests_ident(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Identity hints override ordinary cached-object freshness as configured.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_server(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    for path in ("both", "etag", "lm"):
        request("ident", path, "miss", _ats=_ats, _curl=_curl)
    time.sleep(2)
    request("ident", "both", "hit-fresh", f"Etag {CACHE_RANGE_IDENT_ETAG}", _ats=_ats, _curl=_curl)
    request("ident", "both", "hit-stale", f"Last-Modified {CACHE_RANGE_IDENT_LAST_MODIFIED}", _ats=_ats, _curl=_curl)
    request("ident", "both", "hit-stale", "Etag no_match", _ats=_ats, _curl=_curl)
    request("ident", "etag", "hit-fresh", f"Etag {CACHE_RANGE_IDENT_ETAG}", _ats=_ats, _curl=_curl)
    request("ident", "etag", "hit-stale", f"Last-Modified {CACHE_RANGE_IDENT_LAST_MODIFIED}", _ats=_ats, _curl=_curl)
    request("ident", "etag", "hit-stale", "Etag no_match", _ats=_ats, _curl=_curl)
    request("ident", "lm", "hit-fresh", f"Last-Modified {CACHE_RANGE_IDENT_LAST_MODIFIED}", _ats=_ats, _curl=_curl)
    request("ident", "lm", "hit-stale", f"Etag {CACHE_RANGE_IDENT_ETAG}", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "miss", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-fresh", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-stale", "Etag not_the_same", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-stale", f"Last-Modified {CACHE_RANGE_IDENT_LAST_MODIFIED}", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-fresh", "Etag fresh", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-fresh", _ats=_ats, _curl=_curl)
    request("ident", "fresh", "hit-stale", "Stale", _ats=_ats, _curl=_curl)
    request("identheader", "custom", "miss", _ats=_ats, _curl=_curl)
    request("identheader", "custom", "hit-fresh", "Etag foo", _ats=_ats, _curl=_curl)
