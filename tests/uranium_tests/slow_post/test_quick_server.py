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

from dataclasses import dataclass
from pathlib import Path
import os
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[1] / "tools"


@dataclass(frozen=True)
class QuickServerCase:
    """Describe when the client or origin aborts a slow POST."""

    abort_request: bool
    drain_request: bool
    abort_response_headers: bool

    @property
    def name(self) -> str:
        return (
            f"client-{'abort' if self.abort_request else 'finish'}-"
            f"origin-{'drain' if self.drain_request else 'close'}-"
            f"headers-{'abort' if self.abort_response_headers else 'complete'}")


CASES = tuple(
    QuickServerCase(abort_request, drain_request, abort_response_headers)
    for abort_request in (True, False)
    for drain_request in (True, False)
    for abort_response_headers in (True, False))


class QuickServerScenario:
    """Have the origin answer before it receives the full slow POST body."""

    def __init__(self, case: QuickServerCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._case = case
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def python_environment() -> dict[str, str]:
        """Make the shared HTTP parsing helper importable by both scripts."""

        inherited = os.environ.get("PYTHONPATH", "")
        python_path = str(TEST_TOOLS) if not inherited else f"{TEST_TOOLS}{os.pathsep}{inherited}"
        return {**os.environ, "PYTHONPATH": python_path}

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the purpose-built early-response origin."""

        command: list[str | Path] = [
            sys.executable,
            TEST_DIRECTORY / "quick_server.py",
            "127.0.0.1",
            str(self._origin_port),
        ]
        if self._case.drain_request:
            command.append("--drain-request")
        if self._case.abort_response_headers:
            command.append("--abort-response-headers")
        return services.process(
            "origin",
            command,
            environment=self.python_environment(),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Proxy directly to the early-response origin."""

        ats = ats_factory.create("ts")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin_port}")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|hostdb",
        })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Configure the slow POST client and its optional request abort."""

        command: list[str | Path] = [
            sys.executable,
            TEST_DIRECTORY / "slow_post_client.py",
            "127.0.0.1",
            str(self._ats.http_port),
        ]
        if not self._case.abort_request:
            command.append("--finish-request")
        return services.process("client", command, environment=self.python_environment())

    def verify(self, result: CommandResult) -> None:
        """Require a complete response only when neither peer aborts it."""

        assert result.returncode == 0, result.output
        if self._case.abort_request or self._case.abort_response_headers:
            assert "HTTP/1.1 200 OK" not in result.output
        else:
            assert "HTTP/1.1 200 OK" in result.output

    def run(self) -> None:
        """Start the topology and execute the slow POST client."""

        self._origin.start()
        self._ats.start()
        self.verify(self._client.run(timeout=10))


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.name)
def test_quick_server(case: QuickServerCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS handles origins that answer before receiving a full request."""

    QuickServerScenario(case, ats_factory, services).run()
