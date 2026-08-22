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


class SlowPostScenario:
    """Fill the origin connection limit with slow POST requests."""

    _origin_connection_limit = 3

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Accept the slow POSTs and the final health request."""

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

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install request buffering and cap connections to the origin."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("request_buffer.so"):
            pytest.skip("request_buffer.so is required")
        ats.plugin_config.add_line("request_buffer.so")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.per_server.connection.max": self._origin_connection_limit,
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Start the purpose-built concurrent slow-POST driver."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "slow_post_clients.py",
                "--port",
                str(self._ats.http_port),
                "--connectionlimit",
                str(self._origin_connection_limit),
            ),
        )

    @staticmethod
    def verify(result: CommandResult) -> None:
        """Require the final request to succeed despite the slow POSTs."""

        assert result.returncode == 0, result.output
        assert result.stdout.strip().endswith("200"), result.output

    def run(self) -> None:
        """Start the origin and ATS, then execute the attack simulation."""

        self._origin.start()
        self._ats.start()
        self.verify(self._client.run(timeout=30))


def test_slow_post(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS still serves requests when slow POSTs occupy origin connections."""

    SlowPostScenario(ats_factory, services).run()
