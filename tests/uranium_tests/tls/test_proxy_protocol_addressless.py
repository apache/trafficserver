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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
HOST = "addressless.proxy.protocol.test"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve the request after ATS consumes the PROXY header.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": f"GET /proxy_protocol HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "ok"
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer, _use_tls: bool) -> ATS:
    """Enable a PROXY-protocol listener for the selected transport.

    :param _origin: Test-local origin configured by the test.
    :param _use_tls: Test-local use tls configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create(
        "ts",
        enable_tls=_use_tls,
        enable_cache=False,
        enable_proxy_protocol=True,
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    ats.records.update(
        {
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol",
        })
    return ats


def configure_client(
        services: ServiceFactory, *, _ats: ATS, _origin: OriginServer, _protocol_version: int, _use_tls: bool) -> ProcessService:
    """Configure the custom client for v1 UNKNOWN or v2 LOCAL.

    :param _ats: Test-local ats configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _protocol_version: Test-local protocol version configured by the test.
    :param _use_tls: Test-local use tls configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    port = _ats.proxy_protocol_https_port if _use_tls else _ats.proxy_protocol_port
    arguments: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "proxy_protocol_client.py",
        "127.0.0.1",
        str(port),
        HOST,
        "127.0.0.1",
        "127.0.0.1",
        "60123",
        str(_origin.port),
        str(_protocol_version),
        "--addressless",
    ]
    if _use_tls:
        arguments.append("--https")
    return services.process("client", arguments)


@pytest.mark.parametrize("protocol_version", [1, 2])
@pytest.mark.parametrize("use_tls", [False, True], ids=["http", "tls"])
def test_proxy_protocol_addressless(
    protocol_version: int,
    use_tls: bool,
    ats_factory: ATSFactory,
    services: ServiceFactory,
) -> None:
    """Addressless PROXY protocol headers are accepted before HTTP.

    :param protocol_version: Protocol version used by this test step.
    :param use_tls: Use tls used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin, _use_tls=use_tls)
    _client = configure_client(services, _ats=_ats, _origin=_origin, _protocol_version=protocol_version, _use_tls=use_tls)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=10)
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.output
