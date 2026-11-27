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
HOST = "late.proxy.protocol.test"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve the request that follows the PROXY header and TLS handshake.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": f"GET /proxy_protocol HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n", "body": ""},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "ok"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Enable a PROXY-protocol TLS listener.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create(
        "ts",
        enable_tls=True,
        enable_cache=False,
        enable_proxy_protocol=True,
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    ats.records.update(
        {
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol|ssl",
        }
    )
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _origin: OriginServer, _protocol_version: int) -> ProcessService:
    """Configure a client that sends the PROXY header well after connecting.

    On macOS ATS reads a new connection before any data arrives, and the
    delay makes that first read come back empty.

    :param services: Factory owning support services and their cleanup.
    :param _ats: Test-local ats configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _protocol_version: PROXY protocol version the client sends.
    """

    arguments: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "proxy_protocol_client.py",
        "127.0.0.1",
        str(_ats.proxy_protocol_https_port),
        HOST,
        "192.0.2.7",
        "127.0.0.1",
        "60123",
        str(_origin.port),
        str(_protocol_version),
        "--https",
        "--delay",
        "0.5",
    ]
    return services.process("client", arguments)


@pytest.mark.parametrize("protocol_version", [1, 2])
def test_proxy_protocol_late_header(protocol_version: int, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A PROXY header arriving after ATS's first handshake read is still parsed.

    :param protocol_version: PROXY protocol version the client sends.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = configure_client(services, _ats=_ats, _origin=_origin, _protocol_version=protocol_version)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=10)
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.output
