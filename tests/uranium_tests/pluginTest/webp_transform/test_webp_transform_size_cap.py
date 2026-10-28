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

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines

BIG_IMAGE_SIZE = 20 * 1024 * 1024


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve a 20-MiB image/jpeg body.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET /huge.jpg HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers": ("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
                        f"Content-Length: {BIG_IMAGE_SIZE}\r\n\r\n"),
            "body": "A" * BIG_IMAGE_SIZE,
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Enable the WebP transform on clear-text and HTTP/2 TLS ingress.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    if not ats.plugin_exists("webp_transform.so"):
        pytest.skip("webp_transform.so is required")
    ats.plugin_config.add_line("webp_transform.so convert_to_webp")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "webp_transform",
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def verify(result: CommandResult, status: str) -> None:
    """Require a successful, truthfully typed passthrough response.

    :param result: Completed command result to validate.
    :param status: Status used by this test step.
    """

    assert result.returncode == 0, result.output
    assert status in result.stdout
    assert re.search(r"content-type: image/jpeg", result.stdout, re.IGNORECASE)
    assert "image/webp" not in result.stdout.lower()


def test_webp_transform_size_cap(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """webp_transform declines oversized bodies before buffering or decoding them.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    h1 = curl.get(
        _ats,
        "/huge.jpg",
        headers={"Accept": "image/webp"},
        options=f"--http1.1 --silent --show-error --dump-header - --output /dev/null",
        timeout=30,
    )
    verify(h1, "HTTP/1.1 200")
    h2 = curl.run(
        (
            f"--http2 --insecure --silent --show-error --dump-header - --output /dev/null --header "
            f"'Accept: image/webp' 'https://127.0.0.1:{_ats.https_port}/huge.jpg'"),
        timeout=30,
    )
    verify(h2, "HTTP/2 200")
    wait_for_file_lines(_ats.traffic_out, "exceeds cap 16777216", 2)
