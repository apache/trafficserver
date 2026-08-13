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
"""Unit tests for procedural Uranium service objects."""

from pathlib import Path
import subprocess
from types import SimpleNamespace
from typing import Any

import pytest

from uranium_testkit.services import ATS, Curl


class FakeProcess:
    """Record ATS lifecycle calls without starting a process."""

    def __init__(self) -> None:
        self.Variables = type("Variables", (), {"port": 12345})()
        self.Disk = SimpleNamespace(records_config=FakeRecords())
        self.was_started = False
        self.was_stopped = False
        self.was_validated = False

    def start(self) -> None:
        self.was_started = True

    def stop(self) -> None:
        self.was_stopped = True

    def validate(self) -> None:
        self.was_validated = True

    def is_running(self) -> bool:
        return self.was_started and not self.was_stopped


class FakeRecords:
    """Record staged records.yaml updates."""

    def __init__(self) -> None:
        self.values: dict[str, Any] = {}

    def update(self, values: dict[str, Any]) -> None:
        self.values.update(values)


class FakeScenario:
    """Return a single fake ATS process."""

    def __init__(self) -> None:
        self.process = FakeProcess()

    def MakeATSProcess(self, name: str) -> FakeProcess:  # noqa: N802
        assert name == "ats"
        return self.process


def test_ats_owns_process_lifecycle() -> None:
    """Start, stop, and validate the fixture-owned ATS process."""

    scenario = FakeScenario()
    ats = ATS(scenario)  # type: ignore[arg-type]

    assert ats.http_port == 12345
    assert not ats.is_running

    ats.records.update({"proxy.config.http.server_ports": "12345"})
    assert scenario.process.Disk.records_config.values == {"proxy.config.http.server_ports": "12345"}

    ats.start()
    assert ats.is_running

    ats.close()
    assert scenario.process.was_stopped
    assert scenario.process.was_validated


def test_curl_returns_a_plain_command_result(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Expose curl status and output for ordinary pytest assertions."""

    observed: dict[str, Any] = {}

    def run(command: tuple[str, ...], **kwargs: Any) -> subprocess.CompletedProcess[str]:
        observed["command"] = command
        observed.update(kwargs)
        return subprocess.CompletedProcess(command, 7, "response", "diagnostic")

    monkeypatch.setattr("uranium_testkit.services.subprocess.run", run)

    result = Curl(tmp_path).get("http://127.0.0.1:12345", headers={"X-Test": "value"})

    assert observed["command"] == ("curl", "--header", "X-Test: value", "http://127.0.0.1:12345")
    assert observed["cwd"] == tmp_path
    assert result.returncode == 7
    assert result.output == "responsediagnostic"
