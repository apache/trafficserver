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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent
TCP_CLIENT = TEST_DIRECTORY.parents[1] / "tools" / "tcp_client.py"


class RequestTimeoutScenario:
    """Leave an HTTP request body incomplete until ATS times it out."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Provide the mapped origin, which the incomplete request never reaches."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: www.http408.test\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Use a short inbound transaction inactivity timeout."""

        ats = ats_factory.create("ts")
        ats.remap_config.add_line(f"map http://www.http408.test http://127.0.0.1:{self._origin.port}")
        ats.records.update({"proxy.config.http.transaction_no_activity_timeout_in": 2})
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Use the raw client so the declared body remains unfinished."""

        return services.process(
            "timeout-client",
            (
                sys.executable,
                TCP_CLIENT,
                "127.0.0.1",
                str(self._ats.http_port),
                TEST_DIRECTORY / "data" / "www.http408.test.txt",
                "--delay-after-send",
                "4",
            ),
        )

    def run(self) -> None:
        """Execute the incomplete request and compare the 408 response."""

        self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=10)
        assert_matches_gold(result.stdout, TEST_DIRECTORY / "http408.gold")


def test_http408(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An incomplete request receives ATS's standard 408 response."""

    if curl.uses_uds:
        pytest.skip("the raw TCP client requires a TCP listener")
    RequestTimeoutScenario(ats_factory, services).run()
