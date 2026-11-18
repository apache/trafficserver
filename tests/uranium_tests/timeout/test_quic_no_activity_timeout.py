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
import json
from pathlib import Path
import re
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
H3_IDLE_CLIENT = TEST_DIRECTORY.parent / "h3" / "py_h3_client" / "h3_idle_client.py"

# How long the client stays silent after its first response, in seconds.
QUIET_SECONDS = 5

# Allowance for qlog timestamps when comparing a gap to a timeout, in milliseconds.
QLOG_SLACK_MS = 50


@dataclass(frozen=True)
class IdleTimeoutCase:
    """Describe one QUIC idle timeout configuration."""

    name: str
    no_activity_timeout_in: int
    default_inactivity_timeout: int | None = None
    requires_quiche: bool = True

    @property
    def expect_idle_close(self) -> bool:
        return self.no_activity_timeout_in > 0


CASES = (
    IdleTimeoutCase("disabled", 0, requires_quiche=False),
    IdleTimeoutCase("idle-timeout", 3000),
    # QUIC must use its own timeout, not proxy.config.net.default_inactivity_timeout.
    IdleTimeoutCase("ignores-default", 3000, default_inactivity_timeout=1),
)


def configure_origin(services: ServiceFactory) -> VerifierServer:
    """Serve the request the client sends before and after its quiet period.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("origin", "replay/quic_no_activity_timeout.replay.yaml", https_ports=[])


def configure_ats(ats_factory: ATSFactory, *, _case: IdleTimeoutCase, _origin: VerifierServer) -> ATS:
    """Enable HTTP/3, a qlog per connection, and the case's timeouts.

    :param _case: Test-local case configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_quic=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic|inactivity_cop",
            "proxy.config.quic.qlog.file_base": str(ats.log_directory / "qlog"),
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            "proxy.config.quic.no_activity_timeout_in": _case.no_activity_timeout_in,
        }
    )
    if _case.default_inactivity_timeout is not None:
        ats.records.update({"proxy.config.net.default_inactivity_timeout": _case.default_inactivity_timeout})
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _case: IdleTimeoutCase) -> ProcessService:
    """Build the aioquic client that goes quiet after one request.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    command: list[str | Path] = [
        sys.executable,
        H3_IDLE_CLIENT,
        "--addr",
        f"127.0.0.1:{_ats.https_port}",
        "--authority",
        f"example.com:{_ats.https_port}",
        "--server-name",
        "example.com",
        "--quiet",
        str(QUIET_SECONDS),
    ]
    if not _case.expect_idle_close:
        command.append("--request-after-quiet")
    return services.process("client", command)


def read_qlog_events(content: str) -> list[dict]:
    """Parse the complete events of a JSON-SEQ qlog file that may still be growing.

    :param content: Text of the qlog file.
    """

    # Each record ends with a newline, so a record without one is still being written.
    records = (json.loads(record) for record in content.split("\x1e") if record.endswith("\n"))
    return [record for record in records if "name" in record]


def verify(result: CommandResult, *, _ats: ATS, _case: IdleTimeoutCase) -> None:
    """Require the idle close, or its absence, on the connection the client used.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    match = re.search(r"^server connection id ([0-9a-f]+)$", result.stdout, re.MULTILINE)
    assert match, result.output
    if not _case.expect_idle_close:
        assert f"ok request after {QUIET_SECONDS} seconds of silence" in result.stdout, result.output
        return

    # quiche writes one qlog per connection, named for the connection ID it
    # chose. The file is buffered, so wait for the whole close record.
    qlog = _ats.log_directory / f"qlog-{match.group(1)}.sqlog"
    events = read_qlog_events(wait_for_file_lines(qlog, r'"name":"connectivity:connection_closed".*\n', 1))
    closed = next(event for event in events if event["name"] == "connectivity:connection_closed")
    packets = [
        event
        for event in events
        if event["name"] in ("transport:packet_received", "transport:packet_sent") and event["time"] <= closed["time"]
    ]
    # An idle timeout closes the connection silently: neither side sent CONNECTION_CLOSE.
    close_frames = [
        event for event in packets if any(frame["frame_type"] == "connection_close" for frame in event["data"].get("frames", ()))
    ]
    assert not close_frames, close_frames
    last_received = max(event["time"] for event in packets if event["name"] == "transport:packet_received")
    silence = closed["time"] - last_received
    assert silence >= _case.no_activity_timeout_in - QLOG_SLACK_MS, f"closed after {silence:.0f} ms of silence"


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.name)
def test_quic_no_activity_timeout(case: IdleTimeoutCase, ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS closes a silent QUIC connection after proxy.config.quic.no_activity_timeout_in.

    :param case: Case used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not ats_factory.has_feature("TS_USE_QUIC"):
        pytest.skip("ATS was built without QUIC")
    if case.requires_quiche and not ats_factory.has_feature("TS_HAS_QUICHE"):
        pytest.skip("ATS with quiche is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _case=case, _origin=_origin)
    _client = configure_client(services, _ats=_ats, _case=case)

    _origin.start()
    _ats.start()
    verify(_client.run(timeout=30), _ats=_ats, _case=case)
