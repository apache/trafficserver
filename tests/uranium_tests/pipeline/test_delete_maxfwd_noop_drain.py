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

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class DeleteNoopDrainScenario:
    """Verify a cache-miss DELETE self-response drains its request body."""

    _hostname = "www.example.com"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the purpose-built origin that detects a smuggled request."""

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "desync_server.py", "127.0.0.1", str(self._origin_port)),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable cache handling for the DELETE NOOP path."""

        ats = ats_factory.create("ts", enable_cache=True)
        ats.remap_config.add_line(f"map http://{self._hostname}/ http://127.0.0.1:{self._origin_port}/")
        ats.records.update({"proxy.config.http.cache.http": 1})
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Drive the body-desynchronization probe over one connection."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "desync_client_miss.py",
                "127.0.0.1",
                str(self._ats.http_port),
                self._hostname,
            ),
        )

    @staticmethod
    def verify(result: CommandResult, origin_output: str) -> None:
        """Require the NOOP response and reject all desynchronization signatures."""

        assert result.returncode == 0, result.output
        assert "DELETE_STATUS=404" in result.output
        assert "SECOND_RESPONSE_RECEIVED=True" not in result.output
        assert "misspoison" not in result.output
        assert "misspoison" not in origin_output

    def run(self) -> None:
        """Start the topology and execute the custom client."""

        self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=40)
        self.verify(result, self._origin.output)


def test_delete_maxfwd_noop_drain(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A cache-miss DELETE self-response does not leave request bytes queued."""

    DeleteNoopDrainScenario(ats_factory, services).run()
