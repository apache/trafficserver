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
"""Verify slice reports inconsistent or missing internal blocks."""

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

SLICE_ERROR_BLOCK_BYTES = 9

SLICE_ERROR_BODY = "the quick brown fox"


def add_block(
    origin: OriginServer,
    path: str,
    index: int,
    *,
    etag: str | None = None,
    last_modified: str | None = None,
    total_length: int = 19,
    status: str = "206 Partial Content",
) -> None:
    """Add one internal range response for a malformed object.

    :param origin: Configured origin service.
    :param path: Resource or file path used by this operation.
    :param index: Index used by this test step.
    :param etag: ETag response header value.
    :param last_modified: Last modified used by this test step.
    :param total_length: Total length used by this test step.
    :param status: Status used by this test step.
    """

    begin = index * SLICE_ERROR_BLOCK_BYTES
    end = begin + SLICE_ERROR_BLOCK_BYTES - 1
    body = SLICE_ERROR_BODY[begin:end + 1]
    fields = [f"HTTP/1.1 {status}", "Connection: close"]
    if etag is not None:
        fields.append(f"Etag: {etag}")
    if last_modified is not None:
        fields.append(f"Last-Modified: {last_modified}")
    if status.startswith("206"):
        fields.extend((f"Content-Range: bytes {begin}-{end}/{total_length}", "Cache-Control: max-age=500"))
    origin.add_response(
        {
            "headers":
                (
                    f"GET /{path} HTTP/1.1\r\nHost: ats\r\nRange: bytes={begin}-{end}\r\n"
                    "X-Slicer-Info: full content request\r\n\r\n")
        },
        {
            "headers": "\r\n".join(fields) + "\r\n\r\n",
            "body": body
        },
    )


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create ETag, Last-Modified, Content-Range, and 404 failures.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", lookup_key="{%Range}{PATH}")
    add_block(origin, "etag", 0, etag='"etag0"')
    add_block(origin, "etag", 1, etag='"etag1"')
    add_block(origin, "lastmodified", 0, last_modified="Tue, 08 May 2018 15:49:41 GMT")
    add_block(origin, "lastmodified", 1, last_modified="Tue, 08 Apr 2019 18:00:00 GMT")
    add_block(origin, "crr", 0, etag="crr")
    add_block(origin, "crr", 1, etag="crr", total_length=18)
    add_block(
        origin,
        "internal404",
        0,
        etag='"etag"',
        last_modified="Tue, 08 May 2018 15:49:41 GMT",
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure slice with nine-byte test blocks and no cache.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats", enable_cache=False)
    if not ats.plugin_exists("slice.so"):
        pytest.skip("slice.so is not installed")
    ats.remap_config.add_line(
        f"map / http://127.0.0.1:{_origin.port} @plugin=slice.so @pparam=--blockbytes-test={SLICE_ERROR_BLOCK_BYTES}")
    return ats


def request(path: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request one malformed object and return headers plus partial body.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    """

    result = _curl.get(
        _ats,
        f"/{path}",
        headers={"Host": "ats"},
        options=f"--silent --show-error --dump-header -",
    )
    assert "HTTP/1.1 200 OK" in result.stdout, result.output
    assert SLICE_ERROR_BODY[:SLICE_ERROR_BLOCK_BYTES] in result.stdout, result.output
    return result.output


def test_slice_error(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Slice logs the reason it aborts an inconsistent block stream.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_server(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    cases = (
        ("etag", "Mismatch block Etag"),
        ("lastmodified", "Mismatch block Last-Modified"),
        ("crr", "Mismatch/Bad block Content-Range"),
        ("internal404", "404 internal block response"),
    )
    for path, diagnostic in cases:
        request(path, _ats=_ats, _curl=_curl)
        wait_for_file_lines(_ats.diags_log, diagnostic, 1)
