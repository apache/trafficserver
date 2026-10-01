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

from tools.uranium.services import ATS, ATSFactory, CommandResult, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent

SLOW_POST__origin_connection_limit = 3


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Accept the slow POSTs and the final health request.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    origin.add_response(
        {
            "headers":
                ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n"
                 "Host: www.example.com\r\nConnection: keep-alive\r\n\r\n"),
            "body": "a\r\na\r\na\r\n\r\n",
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Install request buffering and cap connections to the origin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("request_buffer.so"):
        pytest.skip("request_buffer.so is required")
    ats.plugin_config.add_line("request_buffer.so")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.per_server.connection.max": SLOW_POST__origin_connection_limit,
        })
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Start the purpose-built concurrent slow-POST driver.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "slow_post_clients.py",
            "--port",
            str(_ats.http_port),
            "--connectionlimit",
            str(SLOW_POST__origin_connection_limit),
        ),
    )


def verify(result: CommandResult) -> None:
    """Require the final request to succeed despite the slow POSTs.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert result.stdout.strip().endswith("200"), result.output


def test_slow_post(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS still serves requests when slow POSTs occupy origin connections.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    verify(_client.run(timeout=30))
