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

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
IP_ALLOW_CONTENT = """ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods:
      - DELETE
"""


class PipelinedRequestsScenario:
    """Send three pipelined requests while denying the final DELETE."""

    def __init__(self, buffer_requests: bool, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._buffer_requests = buffer_requests
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the origin that understands the test's pipelined framing."""

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "pipeline_server.py", "127.0.0.1", str(self._origin_port)),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure reverse proxying and optionally client request buffering."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin_port}")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ip_allow",
        })
        if self._buffer_requests:
            ats.records.update({"proxy.config.http.request_buffer_enabled": 1})
        ats.write_config_file("ip_allow.yaml", IP_ALLOW_CONTENT)
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Send the three purpose-built requests on one connection."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "pipeline_client.py",
                "127.0.0.1",
                str(self._ats.http_port),
                "server.com",
                "server.com",
            ),
        )

    def verify(self, result: CommandResult) -> None:
        """Require two origin responses and one ATS-generated denial."""

        assert result.returncode == 0, result.output
        assert "X-Response: first" in result.output
        assert "X-Response: second" in result.output
        assert "X-Response: third" not in result.output
        assert "403" in result.output
        assert "/first" in self._origin.output
        assert "/second" in self._origin.output
        assert "/third" not in self._origin.output

    def run(self) -> None:
        """Start the topology and execute the pipelined client."""

        self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=10)
        self._origin.wait(timeout=5)
        self.verify(result)


class RequestFramingScenario:
    """Verify body-less and conflicting Content-Length request framing."""

    def __init__(self, mode: str, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._mode = mode
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the origin that records exact request boundaries."""

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "request_framing_server.py", "127.0.0.1", str(self._origin_port)),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a cache-free reverse proxy to the recording origin."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin_port}/")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Send the selected request shape over a raw socket."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "request_framing_client.py",
                "127.0.0.1",
                str(self._ats.http_port),
                "www.example.com",
                self._mode,
            ),
        )

    def verify(self, result: CommandResult) -> None:
        """Verify client responses and the request boundaries seen by the origin."""

        assert result.returncode == 0, result.output
        origin_output = self._origin.output
        if self._mode == "pipeline":
            assert "STATUS_LINE_COUNT: 2" in result.output
            assert "X-Origin-Response: first" in result.output
            assert "X-Origin-Response: second" in result.output
            assert "REQUEST_LINE: POST / HTTP/1.1" in origin_output
            assert "REQUEST_LINE: GET /second HTTP/1.1" in origin_output
            assert "ORIGIN_REQUEST_COUNT: 2" in origin_output
            assert "ORIGIN_REQUEST_COUNT: 3" not in origin_output
            assert re.search(r"BODY:.*GET /second", origin_output) is None
            assert re.search(r"BODY:.*X-Marker", origin_output) is None
        else:
            assert "HTTP/1.1 400" in result.output
            assert "STATUS_LINE_COUNT: 1" in result.output
            assert "X-Origin-Response: second" not in result.output
            assert "REQUEST_LINE:" not in origin_output

    def run(self) -> None:
        """Start the topology and execute the framing client."""

        self._origin.start()
        self._ats.start()
        self.verify(self._client.run(timeout=20))


@pytest.mark.parametrize("buffer_requests", (False, True), ids=("streaming", "buffered"))
def test_pipelined_requests(buffer_requests: bool, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS handles pipelined chunked requests with and without buffering."""

    PipelinedRequestsScenario(buffer_requests, ats_factory, services).run()


@pytest.mark.parametrize("mode", ("pipeline", "conflicting_cl"))
def test_request_framing(mode: str, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS preserves valid framing and rejects conflicting Content-Length fields."""

    RequestFramingScenario(mode, ats_factory, services).run()
