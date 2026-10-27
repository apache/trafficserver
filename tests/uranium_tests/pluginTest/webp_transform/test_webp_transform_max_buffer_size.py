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

TWO_MIB = 2 * 1024 * 1024


def webp_buffer_override_configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve a two-MiB image body.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET /two_mib.jpg HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers":
                ("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
                 f"Content-Length: {TWO_MIB}\r\nConnection: close\r\n\r\n"),
            "body": "A" * TWO_MIB,
        },
    )
    return origin


def webp_buffer_override_configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Set max_buffer_size with the M suffix.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("webp_transform.so"):
        pytest.skip("webp_transform.so is required")
    ats.plugin_config.add_line("webp_transform.so convert_to_webp max_buffer_size=1M")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "webp_transform",
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def webp_buffer_override_verify(result: CommandResult) -> None:
    """Require a truthful passthrough response.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200" in result.stdout
    assert re.search(r"content-type: image/jpeg", result.stdout, re.IGNORECASE)


def webp_invalid_buffer_size_configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure negative, bad-suffix, and multiplication-overflow values.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("webp_transform.so"):
        pytest.skip("webp_transform.so is required")
    ats.plugin_config.add_line(
        "webp_transform.so convert_to_webp max_buffer_size=-1 "
        "max_buffer_size=8X max_buffer_size=20000000000G")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "webp_transform",
    })
    return ats


def test_webp_transform_max_buffer_size(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An M-suffixed max_buffer_size overrides the default transform cap.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = webp_buffer_override_configure_origin(services)
    _ats = webp_buffer_override_configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.get(
        _ats,
        "/two_mib.jpg",
        headers={"Accept": "image/webp"},
        options=f"--silent --show-error --dump-header - --output /dev/null",
    )
    webp_buffer_override_verify(result)
    wait_for_file_lines(_ats.traffic_out, "exceeds cap 1048576", 1)


def test_webp_transform_invalid_buffer_sizes(ats_factory: ATSFactory) -> None:
    """Malformed max_buffer_size values cannot disable the safe default.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = webp_invalid_buffer_size_configure_ats(ats_factory)

    _ats.start()
    diags = wait_for_file_lines(_ats.diags_log, "invalid max_buffer_size=", 3)
    for value in ("-1", "8X", "20000000000G"):
        assert f"invalid max_buffer_size={value}, keeping default 16777216" in diags
