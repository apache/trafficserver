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

from pathlib import Path
import re
import ssl
import sys

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent
OVERSIZED_SIZE = 70000


def configure_origin(services: ServiceFactory) -> VerifierServer:
    """Serve only the normal request and detect accidental oversized forwarding.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server(
        "origin",
        "replay/oversized_field_h2.replay.yaml",
        https_ports=[],
    )


def configure_ats(ats_factory: ATSFactory, *, _origin: VerifierServer) -> ATS:
    """Raise the list limit while retaining the uint16 field-size ceiling.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2|hpack",
            "proxy.config.http.header_field_max_size": 65535,
            "proxy.config.http2.max_header_list_size": 8 * 1024 * 1024,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    return ats


def configure_client(
        name: str, path: str, name_size: int, value_size: int, *, _ats: ATS, _services: ServiceFactory) -> ProcessService:
    """Configure one invocation of the raw HPACK client.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param path: Resource or file path used by this operation.
    :param name_size: Header name length in bytes.
    :param value_size: Header value length in bytes.
    """

    return _services.process(
        name,
        (
            sys.executable,
            TEST_DIRECTORY / "clients" / "oversized_field_h2_client.py",
            path,
            str(name_size),
            str(value_size),
            "127.0.0.1",
            str(_ats.https_port),
            "example.com",
        ),
    )


def verify_rejection(result: CommandResult) -> None:
    """Require a connection error rather than an HTTP response.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert "status=None" in result.output
    assert "goaway_error=9" in result.output


def verify_normal(result: CommandResult) -> None:
    """Require an ordinary under-limit request to be proxied.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert "status=200" in result.output


def test_oversized_field_h2(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Oversized HTTP/2 fields produce GOAWAY COMPRESSION_ERROR and never reach the origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if ssl.OPENSSL_VERSION_INFO < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    if not services.proxy_verifier_at_least("2.8.0"):
        pytest.skip("Proxy Verifier 2.8.0 or newer is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    value_client = configure_client("oversized-value", "/h2-oversized-value", 0, OVERSIZED_SIZE, _ats=_ats, _services=services)
    name_client = configure_client("oversized-name", "/h2-oversized-name", OVERSIZED_SIZE, 0, _ats=_ats, _services=services)
    normal_client = configure_client("normal", "/h2-normal", 0, 0, _ats=_ats, _services=services)
    verify_rejection(value_client.run(timeout=15))
    verify_rejection(name_client.run(timeout=15))
    verify_normal(normal_client.run(timeout=15))
    wait_for_file_lines(
        _ats.diags_log,
        r"ERROR: HTTP/2 connection error code=0x09 .* compression error",
        2,
    )
    origin_output = _origin.output
    assert re.search(r"h2-normal", origin_output)
    assert "h2-oversized-value" not in origin_output
    assert "h2-oversized-name" not in origin_output
