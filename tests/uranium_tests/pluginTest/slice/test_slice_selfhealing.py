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
"""Verify slice repairs inconsistent cached blocks."""

from datetime import UTC, datetime, timedelta
import shlex
from email.utils import format_datetime
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines


def add_range_response(origin: OriginServer, uid: str, byte_range: str, etag: str, body: str) -> None:
    """Add a cacheable five-byte asset block.

    :param origin: Configured origin service.
    :param uid: Uid used by this test step.
    :param byte_range: Byte range to request, or whether to send the test range.
    :param etag: ETag response header value.
    :param body: HTTP message body.
    """

    start, requested_end = (int(value) for value in byte_range.split("-"))
    end = min(requested_end, 4)
    origin.add_response(
        {"headers": (f"GET {{PATH}} HTTP/1.1\r\nHost: www.example.com\r\nuuid: {uid}\r\n"
                     f"Range: bytes={byte_range}\r\n\r\n")},
        {
            "headers":
                (
                    "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nCache-Control: max-age=5000\r\n"
                    f"Connection: close\r\nContent-Range: bytes {start}-{end}/5\r\nEtag: \"{etag}\"\r\n\r\n"),
            "body": body,
        },
    )


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create old, new, non-range, missing, and custom-identity responses.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", lookup_key="{%uuid}")
    for uid, byte_range, etag, body in (
        ("etagold-1", "3-5", "etagold", "aa"),
        ("etagnew-0", "0-2", "etagnew", "bbb"),
        ("etagnew-1", "3-5", "etagnew", "bb"),
        ("etagold-0", "0-2", "etagold", "aaa"),
        ("assetgone-0", "0-2", "etag", "aaa"),
        ("etagold-custom-1", "3-5", "etagold-custom", "aa"),
        ("etagnew-custom-0", "0-2", "etagnew-custom", "bbb"),
        ("etagnew-custom-1", "3-5", "etagnew-custom", "bb"),
    ):
        add_range_response(origin, uid, byte_range, etag, body)
    origin.add_response(
        {"headers": ("GET /code200 HTTP/1.1\r\nHost: www.example.com\r\nuuid: code200\r\n"
                     "Range: bytes=3-5\r\n\r\n")},
        {
            "headers": ("HTTP/1.1 200 OK\r\nCache-Control: max-age=5000\r\nConnection: close\r\nEtag: \"etag\"\r\n\r\n"),
            "body": "ccccc",
        },
    )
    origin.add_response(
        {"headers": "GET {PATH} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n",
            "body": "Not Found"
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure default and custom identity repair chains.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats")
    required = ("slice.so", "cache_range_requests.so", "xdebug.so")
    if not all(ats.plugin_exists(plugin) for plugin in required):
        pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")
    origin = f"http://127.0.0.1:{_origin.port}/"
    ats.remap_config.add_lines(
        (
            f"map http://slice/ {origin} @plugin=slice.so @pparam=--blockbytes-test=3 @pparam=--remap-host=crr",
            f"map http://crr/ {origin} @plugin=cache_range_requests.so @pparam=--consider-ident",
            f"map http://slicehdr/ {origin} @plugin=slice.so @pparam=--blockbytes-test=3 "
            "@pparam=--remap-host=crrhdr @pparam=--crr-ident-header=crr-foo",
            f"map http://crrhdr/ {origin} @plugin=cache_range_requests.so @pparam=--ident-header=crr-foo",
        ))
    ats.plugin_config.add_line("xdebug.so --enable=x-cache")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "cache_range_requests|slice",
    })
    return ats


def request(
        host: str,
        path: str,
        *,
        byte_range: str | None = None,
        uid: str | None = None,
        identity: str | None = None,
        show_download_size: bool = False,
        _ats: ATS,
        _curl: Curl) -> CommandResult:
    """Issue one range request through the persistent ATS cache.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param path: Resource or file path used by this operation.
    :param byte_range: Byte range to request, or whether to send the test range.
    :param uid: Uid used by this test step.
    :param identity: Identity used by this test step.
    :param show_download_size: Show download size used by this test step.
    """

    arguments = [
        "--silent",
        "--show-error",
        "--dump-header",
        "-",
        "--proxy",
        f"http://127.0.0.1:{_ats.http_port}",
        "--header",
        "x-debug: x-cache",
    ]
    if byte_range is not None:
        arguments.extend(("--range", byte_range))
    if uid is not None:
        arguments.extend(("--header", f"uuid: {uid}"))
    if identity is not None:
        arguments.extend(("--header", f"crr-foo: {identity}"))
    if show_download_size:
        arguments.extend(("--write-out", "SENT: '%{size_download}'"))
    arguments.append(f"http://{host}/{path}")
    return _curl.run_for(
        _ats,
        shlex.join(arguments),
    )


def assert_contains(result: CommandResult, *values: str) -> None:
    """Assert all expected response fragments are present.

    :param result: Completed command result to validate.
    :param values: Values used by this test step.
    """

    for value in values:
        assert value in result.output, result.output


def repair_non_reference_block(*, _ats: ATS, _curl: Curl) -> None:
    """Replace an old second block and continue the response.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    assert_contains(request("crr", "second", byte_range="0-2", uid="etagnew-0", _ats=_ats, _curl=_curl), "bbb", "etagnew")
    assert_contains(request("crr", "second", byte_range="3-5", uid="etagold-1", _ats=_ats, _curl=_curl), "aa", "etagold")
    healed = request("slice", "second", byte_range="3-", uid="etagnew-1", _ats=_ats, _curl=_curl)
    assert healed.returncode == 0, healed.output
    assert_contains(healed, "bb", "etagnew")
    complete = request("slice", "second", _ats=_ats, _curl=_curl)
    assert complete.returncode == 0, complete.output
    assert_contains(complete, "bbbbb", "etagnew")


def repair_reference_block(*, _ats: ATS, _curl: Curl) -> None:
    """Abort an inconsistent response, heal its reference, and retry.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    assert_contains(request("crr", "reference", byte_range="0-2", uid="etagold-0", _ats=_ats, _curl=_curl), "aaa", "etagold")
    assert_contains(request("crr", "reference", byte_range="3-5", uid="etagnew-1", _ats=_ats, _curl=_curl), "bb", "etagnew")
    aborted = request("slice", "reference", byte_range="3-", uid="etagnew-0", show_download_size=True, _ats=_ats, _curl=_curl)
    assert_contains(aborted, "etagold", "SENT: '0'")
    complete = request("slice", "reference", _ats=_ats, _curl=_curl)
    assert complete.returncode == 0, complete.output
    assert_contains(complete, "bbbbb", "etagnew")


def handle_non_range_and_missing_assets(*, _ats: ATS, _curl: Curl) -> None:
    """Pass through a 200 and turn an incomplete cached asset into a 404.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    full = request("slice", "code200", byte_range="3-5", uid="code200", _ats=_ats, _curl=_curl)
    assert full.returncode == 0, full.output
    assert_contains(full, "200 OK", "ccccc")
    seeded = request("slice", "assetgone", byte_range="0-2", uid="assetgone-0", _ats=_ats, _curl=_curl)
    assert seeded.returncode == 0, seeded.output
    assert_contains(seeded, "aaa", "etag")
    incomplete = request("slice", "assetgone", _ats=_ats, _curl=_curl)
    assert_contains(incomplete, "aaa", "Content-Length: 5", "etag")
    for _attempt in range(20):
        missing = request("slice", "assetgone", _ats=_ats, _curl=_curl)
        if "404 Not Found" in missing.output:
            break
        time.sleep(0.1)
    else:
        pytest.fail(f"slice did not discard the incomplete cached asset:\n{missing.output}")


def repair_with_custom_identity(*, _ats: ATS, _curl: Curl) -> None:
    """Repair a block when cache_range_requests uses a custom identity header.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    identity = format_datetime(datetime.now(UTC) + timedelta(seconds=100), usegmt=True)
    old = request("crrhdr", "second-custom", byte_range="3-5", uid="etagold-custom-1", identity=identity, _ats=_ats, _curl=_curl)
    assert_contains(old, "aa", "etagold-custom")
    healed = request("slicehdr", "second-custom", byte_range="3-", uid="etagnew-custom-1", _ats=_ats, _curl=_curl)
    assert healed.returncode == 0, healed.output
    assert_contains(healed, "bb", "etagnew-custom")


def test_slice_selfhealing(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Slice heals stale blocks, missing assets, and custom identities.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_server(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    repair_non_reference_block(_ats=_ats, _curl=_curl)
    repair_reference_block(_ats=_ats, _curl=_curl)
    handle_non_range_and_missing_assets(_ats=_ats, _curl=_curl)
    repair_with_custom_identity(_ats=_ats, _curl=_curl)
    wait_for_file_lines(_ats.diags_log, "logSliceError", 1)
