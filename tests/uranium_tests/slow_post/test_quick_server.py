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
    use_request_transform: bool = False

    @property
    def name(self) -> str:
        """Name."""
        return (
            f"client-{'abort' if self.abort_request else 'finish'}-"
            f"origin-{'drain' if self.drain_request else 'close'}-"
            f"headers-{'abort' if self.abort_response_headers else 'complete'}-"
            f"transform-{'on' if self.use_request_transform else 'off'}")


CASES = tuple(
    QuickServerCase(abort_request, drain_request, abort_response_headers)
    for abort_request in (True, False)
    for drain_request in (True, False)
    for abort_response_headers in (True, False))
CASES += (QuickServerCase(False, False, False, use_request_transform=True),)


def python_environment() -> dict[str, str]:
    """Make the shared HTTP parsing helper importable by both scripts."""

    inherited = os.environ.get("PYTHONPATH", "")
    python_path = str(TEST_TOOLS) if not inherited else f"{TEST_TOOLS}{os.pathsep}{inherited}"
    return {**os.environ, "PYTHONPATH": python_path}


def configure_origin(services: ServiceFactory, *, _case: QuickServerCase, _origin_port: int) -> ProcessService:
    """Start the purpose-built early-response origin.

    :param _case: Test-local case configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    command: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "quick_server.py",
        "127.0.0.1",
        str(_origin_port),
    ]
    if _case.drain_request:
        command.append("--drain-request")
    if _case.abort_response_headers:
        command.append("--abort-response-headers")
    return services.process(
        "origin",
        command,
        environment=python_environment(),
        ready_port=_origin_port,
    )


def configure_ats(ats_factory: ATSFactory, *, _case: QuickServerCase, _origin_port: int) -> ATS:
    """Proxy directly to the early-response origin.

    :param _case: Test-local case configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin_port}")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|dns|hostdb",
    })
    if _case.use_request_transform:
        ats.copy_custom_plugin("{AtsTestPluginsDir}/tunnel_transform.so")
        ats.plugin_config.add_line("tunnel_transform.so request_hdr")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _case: QuickServerCase) -> ProcessService:
    """Configure the slow POST client and its optional request abort.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    script = "partial_post_client.py" if _case.use_request_transform else "slow_post_client.py"
    command: list[str | Path] = [sys.executable, TEST_DIRECTORY / script, "127.0.0.1", str(_ats.http_port)]
    if not _case.use_request_transform and not _case.abort_request:
        command.append("--finish-request")
    return services.process("client", command, environment=python_environment())


def verify(result: CommandResult, *, _case: QuickServerCase) -> None:
    """Require a complete response only when neither peer aborts it.

    :param _case: Test-local case configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    if not _case.use_request_transform and (_case.abort_request or _case.abort_response_headers):
        assert "HTTP/1.1 200 OK" not in result.output
    else:
        assert "HTTP/1.1 200 OK" in result.output


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.name)
def test_quick_server(case: QuickServerCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS handles origins that answer before receiving a full request.

    :param case: Case used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _origin = configure_origin(services, _case=case, _origin_port=_origin_port)
    _ats = configure_ats(ats_factory, _case=case, _origin_port=_origin_port)
    _client = configure_client(services, _ats=_ats, _case=case)

    _origin.start()
    _ats.start()
    verify(_client.run(timeout=10), _case=case)
