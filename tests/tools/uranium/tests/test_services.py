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

from tools.uranium.services import ATS, ATSFactory, Curl


class FakeProcess:
    """Record ATS lifecycle calls without starting a process."""

    def __init__(self, port: int) -> None:
        self.Variables = type("Variables", (), {"port": port, "uds_path": f"/tmp/ats-{port}.sock"})()
        self.Disk = SimpleNamespace(records_config=FakeRecords())
        self.was_started = False
        self.was_stopped = False
        self.was_validated = False
        self.validation_error: Exception | None = None

    def start(self) -> None:
        self.was_started = True

    def stop(self) -> None:
        self.was_stopped = True

    def validate(self) -> None:
        self.was_validated = True
        if self.validation_error is not None:
            raise self.validation_error

    def is_running(self) -> bool:
        return self.was_started and not self.was_stopped


class FakeRecords:
    """Record staged records.yaml updates."""

    def __init__(self) -> None:
        self.values: dict[str, Any] = {}

    def update(self, values: dict[str, Any]) -> None:
        self.values.update(values)


class FakeScenario:
    """Create fake ATS processes with distinct ports."""

    def __init__(self) -> None:
        self.processes: dict[str, FakeProcess] = {}
        self.process_options: dict[str, dict[str, Any]] = {}

    def MakeATSProcess(self, name: str, **options: Any) -> FakeProcess:  # noqa: N802
        process = FakeProcess(12345 + len(self.processes))

        self.processes[name] = process
        self.process_options[name] = options
        return process


def test_ats_owns_process_lifecycle() -> None:
    """Start, stop, and validate the fixture-owned ATS process."""

    scenario = FakeScenario()
    ats = ATS(scenario)  # type: ignore[arg-type]

    assert ats.http_port == 12345
    assert ats.uds_path == "/tmp/ats-12345.sock"
    assert not ats.is_running

    ats.records.update({"proxy.config.http.server_ports": "12345"})
    assert scenario.processes["ats"].Disk.records_config.values == {"proxy.config.http.server_ports": "12345"}

    ats.start()
    assert ats.is_running

    ats.close()
    assert scenario.processes["ats"].was_stopped
    assert scenario.processes["ats"].was_validated


def test_ats_factory_owns_multiple_process_lifecycles() -> None:
    """Create, stop, and validate multiple independent Traffic Servers."""

    scenario = FakeScenario()
    factory = ATSFactory(scenario)  # type: ignore[arg-type]

    first = factory.create("first")
    second = factory.create("second", enable_cache=False)

    assert first.http_port == 12345
    assert second.http_port == 12346
    assert scenario.process_options["second"] == {"enable_cache": False}

    first.start()
    second.start()
    factory.close()

    assert all(process.was_stopped for process in scenario.processes.values())
    assert all(process.was_validated for process in scenario.processes.values())


def test_ats_factory_rejects_duplicate_names() -> None:
    """Keep each public service handle bound to a distinct process."""

    factory = ATSFactory(FakeScenario())  # type: ignore[arg-type]

    factory.create("ats")

    with pytest.raises(ValueError, match="already exists"):
        factory.create("ats")


def test_ats_factory_closes_all_processes_after_validation_failure() -> None:
    """Do not leak one Traffic Server when another fails during teardown."""

    scenario = FakeScenario()
    factory = ATSFactory(scenario)  # type: ignore[arg-type]
    first = factory.create("first")
    second = factory.create("second")
    first.start()
    second.start()
    scenario.processes["second"].validation_error = RuntimeError("validation failed")

    with pytest.raises(ExceptionGroup, match="cleanup failed"):
        factory.close()

    assert all(process.was_stopped for process in scenario.processes.values())
    assert all(process.was_validated for process in scenario.processes.values())


@pytest.mark.parametrize(
    ("use_uds", "transport_arguments"),
    [
        (False, ()),
        (True, ("--unix-socket", "/tmp/ats-12345.sock")),
    ],
)
def test_curl_targets_ats_transport(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    use_uds: bool,
    transport_arguments: tuple[str, ...],
) -> None:
    """Expose curl status and output for ordinary pytest assertions."""

    observed: dict[str, Any] = {}

    def run(command: tuple[str, ...], **kwargs: Any) -> subprocess.CompletedProcess[str]:
        observed["command"] = command
        observed.update(kwargs)
        return subprocess.CompletedProcess(command, 7, "response", "diagnostic")

    monkeypatch.setattr("tools.uranium.services.subprocess.run", run)
    ats = ATS(FakeScenario())  # type: ignore[arg-type]

    result = Curl(tmp_path, use_uds=use_uds).get(ats, "status", headers={"X-Test": "value"})

    assert observed["command"] == (
        "curl",
        *transport_arguments,
        "--header",
        "X-Test: value",
        "http://127.0.0.1:12345/status",
    )
    assert observed["cwd"] == tmp_path
    assert result.returncode == 7
    assert result.output == "responsediagnostic"
