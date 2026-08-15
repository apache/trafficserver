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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent
TCP_CLIENT = TEST_DIRECTORY.parents[1] / "tools" / "tcp_client.py"


class ConnectionFailureScenario:
    """Send a raw request to an origin whose reserved port is closed."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin_port = services.allocate_port()
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(ats_factory, services)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Map the request to the unused origin port."""

        ats = ats_factory.create("ts")
        ats.remap_config.add_line(f"map http://www.connectfail502.test http://127.0.0.1:{self._origin_port}")
        return ats

    def configure_client(self, ats_factory: ATSFactory, services: ServiceFactory) -> ProcessService:
        """Create the raw client request used for stable response formatting."""

        request = ats_factory.run_directory / "connection-failure.request"
        request.write_text("GET / HTTP/1.1\r\nHost: www.connectfail502.test\r\n\r\n")
        return services.process(
            "connection-failure-client",
            (sys.executable, TCP_CLIENT, "127.0.0.1", str(self._ats.http_port), request),
        )

    def run(self) -> None:
        """Start ATS, issue the request, and compare the generated error page."""

        self._ats.start()
        result = self._client.run()
        output = re.sub(r"^(?:Date: |Server: ATS/).*\n", "", result.stdout, flags=re.MULTILINE)
        assert_matches_gold(output, TEST_DIRECTORY / "general-connection-failure-502.gold")


def test_general_connection_failure_502(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """A refused origin connection produces ATS's standard 502 response."""

    if curl.uses_uds:
        pytest.skip("the raw TCP client requires a TCP listener")
    ConnectionFailureScenario(ats_factory, services).run()
