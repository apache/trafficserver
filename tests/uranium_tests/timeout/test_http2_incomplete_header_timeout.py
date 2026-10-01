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
import sys

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent
VC_EVENT_ACTIVE_TIMEOUT = 106
HTTP2_ERROR_COMPRESSION_ERROR = 9


@dataclass(frozen=True)
class IncompleteHeaderCase:
    """Describe one fragmented HTTP/2 request."""

    name: str
    timeout: int
    path: str
    uuid: str
    end_headers: bool = False
    continuation_delay: float | None = None
    min_elapsed: float | None = None
    max_elapsed: float | None = None
    expect_timeout: bool = False


CASES = (
    IncompleteHeaderCase("times-out", 3, "/incomplete", "incomplete_header", min_elapsed=2.5, max_elapsed=7, expect_timeout=True),
    IncompleteHeaderCase("continuation-arrives", 5, "/incomplete", "incomplete_header", continuation_delay=1),
    IncompleteHeaderCase("transaction-started", 2, "/delayed", "delayed_response", end_headers=True, min_elapsed=4.5),
)


def configure_origin(services: ServiceFactory) -> VerifierServer:
    """Serve the transactions that reach an origin after complete headers.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server(
        "origin",
        "replay/http2_incomplete_header_timeout.replay.yaml",
        https_ports=[],
    )


def configure_ats(ats_factory: ATSFactory, *, _case: IncompleteHeaderCase, _origin: VerifierServer) -> ATS:
    """Enable HTTP/2 TLS and configure the incomplete-header timeout.

    :param _case: Test-local case configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2",
            "proxy.config.http2.incomplete_header_timeout_in": _case.timeout,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _case: IncompleteHeaderCase) -> ProcessService:
    """Build the purpose-built frame-level HTTP/2 client command.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    command: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "http2_incomplete_header_client.py",
        str(_ats.https_port),
        "--path",
        _case.path,
        "--uuid",
        _case.uuid,
    ]
    if _case.end_headers:
        command.append("--end-headers")
    if _case.continuation_delay is not None:
        command.extend(("--continuation-delay", str(_case.continuation_delay)))
    if _case.min_elapsed is not None:
        command.extend(("--min-elapsed", str(_case.min_elapsed)))
    if _case.max_elapsed is not None:
        command.extend(("--max-elapsed", str(_case.max_elapsed)))
    return services.process("client", command)


def verify(result: CommandResult, *, _ats: ATS, _case: IncompleteHeaderCase) -> None:
    """Require either the timeout teardown or a normally proxied response.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    timeout_error = "ERROR: HTTP/2 stream error timeout"
    if _case.expect_timeout:
        assert f"GOAWAY error_code={HTTP2_ERROR_COMPRESSION_ERROR} last_stream_id=1" in result.stdout
        wait_for_file_lines(_ats.traffic_out, f"timeout event={VC_EVENT_ACTIVE_TIMEOUT}", 1)
        wait_for_file_lines(_ats.diags_log, timeout_error, 1)
    else:
        assert "stream 1: status=200" in result.stdout
        assert "GOAWAY" not in result.stdout
        assert timeout_error not in _ats.diags_log.read_text(errors="replace")


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.name)
def test_http2_incomplete_header_timeout(
    case: IncompleteHeaderCase,
    ats_factory: ATSFactory,
    services: ServiceFactory,
) -> None:
    """ATS bounds the time spent waiting for an HTTP/2 CONTINUATION frame.

    :param case: Case used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _case=case, _origin=_origin)
    _client = configure_client(services, _ats=_ats, _case=case)

    _origin.start()
    _ats.start()
    verify(_client.run(timeout=15), _ats=_ats, _case=case)
