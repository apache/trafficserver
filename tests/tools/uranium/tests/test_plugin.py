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
"""Unit tests for pytest collection behavior."""

from dataclasses import dataclass, field
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import pytest
import fcntl
import os
import subprocess
import sys

from tools.uranium.plugin import _mark_manual_tests, _update_sandbox_retention, pytest_collection_modifyitems


@pytest.mark.parametrize("workers", [0, 2])
def test_collision_diagnostic_reaches_terminal(tmp_path: Path, workers: int) -> None:
    """Show both conflicting tests even when collection happens in xdist workers.

    :param tmp_path: Temporary collection tree.
    :param workers: Number of subprocess workers, or zero for serial collection.
    """

    directory = tmp_path / "uranium_tests"
    directory.mkdir()
    for name in ("a", "b"):
        (directory / f"test_{name}.py").write_text("def test_duplicate():\n    pass\n")
    environment = {**os.environ, "PYTHONPATH": str(Path(__file__).parents[3])}
    result = subprocess.run(
        [
            sys.executable, "-m", "pytest", "-p", "tools.uranium.plugin", "--import-mode=importlib", "-n",
            str(workers),
            str(directory)
        ],
        cwd=tmp_path,
        env=environment,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode != 0
    assert "Give these tests distinct names" in result.stdout + result.stderr
    assert "test_a.py" in result.stdout + result.stderr
    assert "test_b.py" in result.stdout + result.stderr


def test_concurrent_session_cannot_enter_shared_sandbox(tmp_path: Path) -> None:
    """Reject a second controller before it can reset ports or delete artifacts.

    :param tmp_path: Temporary sandbox root held by the first session.
    """

    evidence = tmp_path / "retained-artifact"
    evidence.write_text("first session")
    environment = {**os.environ, "PYTHONPATH": str(Path(__file__).parents[3])}
    with (tmp_path / ".session-lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        result = subprocess.run(
            [sys.executable, "-m", "pytest", "-p", "tools.uranium.plugin", "--sandbox",
             str(tmp_path), "--collect-only"],
            cwd=tmp_path,
            env=environment,
            capture_output=True,
            text=True,
            timeout=15,
        )
    assert result.returncode == 4
    assert "already in use" in result.stdout + result.stderr
    assert evidence.read_text() == "first session"


def test_duplicate_sandbox_names_fail_collection(monkeypatch: pytest.MonkeyPatch) -> None:
    """Reject colliding names before either test can erase the other's files.

    :param monkeypatch: Fixture isolating collection from serial-test metadata.
    """

    monkeypatch.setattr("tools.uranium.plugin._is_serial_test", lambda path: False)
    config = SimpleNamespace(getoption=lambda name: False)
    items = [
        SimpleNamespace(
            nodeid=f"uranium_tests/{directory}/test_example.py::test_example",
            path=Path(f"uranium_tests/{directory}/test_example.py"),
            add_marker=lambda marker: None,
            get_closest_marker=lambda name: name == "uranium_procedural",
        ) for directory in ("first", "second")
    ]
    with pytest.raises(pytest.UsageError, match="Give these tests distinct names"):
        pytest_collection_modifyitems(config, items)


@dataclass
class FakeItem:
    """Record markers applied to one synthetic pytest item."""

    is_manual: bool
    manual_reason: str | None = None
    markers: list[Any] = field(default_factory=list)

    def get_closest_marker(self, name: str) -> pytest.Mark | None:
        """Return a marker only for manual synthetic items."""

        if name != "manual" or not self.is_manual:
            return None
        return pytest.mark.manual(reason=self.manual_reason).mark

    def add_marker(self, marker: Any, append: bool = True) -> None:
        """Record one marker added by the collection hook."""

        del append
        self.markers.append(marker)


def test_manual_tests_are_skipped_by_default() -> None:
    """Keep opt-in tests visible without executing them in normal runs."""

    manual = FakeItem(is_manual=True)
    regular = FakeItem(is_manual=False)

    _mark_manual_tests([manual, regular], enabled=False)

    assert [marker.mark.name for marker in manual.markers] == ["skip"]
    assert "--run-manual" in manual.markers[0].mark.kwargs["reason"]
    assert regular.markers == []


def test_manual_skip_preserves_the_marker_reason() -> None:
    """Explain why an opt-in scenario is excluded from normal runs."""

    manual = FakeItem(is_manual=True, manual_reason="requires root")

    _mark_manual_tests([manual], enabled=False)

    assert manual.markers[0].mark.kwargs["reason"] == "requires root; pass --run-manual to execute it"


def test_run_manual_enables_manual_tests() -> None:
    """Do not add a skip when the explicit opt-in flag is present."""

    manual = FakeItem(is_manual=True)

    _mark_manual_tests([manual], enabled=True)

    assert manual.markers == []


@pytest.mark.parametrize(
    ("did_fail", "keep_sandboxes", "is_retained"),
    ((False, False, False), (True, False, True), (False, True, True)),
)
def test_procedural_sandbox_retention_policy(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    did_fail: bool,
    keep_sandboxes: bool,
    is_retained: bool,
) -> None:
    """Keep failed sandboxes and explicitly retained successful sandboxes.

    :param monkeypatch: Pytest fixture used to provide a synthetic runtime.
    :param tmp_path: Temporary directory containing the item sandbox.
    :param did_fail: Whether the synthetic call phase failed.
    :param keep_sandboxes: Whether successful sandboxes were explicitly requested.
    :param is_retained: Whether the sandbox should remain after teardown.
    """

    sandbox = tmp_path / "sandbox"
    sandbox.mkdir()

    class Runtime:
        """Return the sandbox owned by the synthetic item."""

        @staticmethod
        def procedural_sandbox(_nodeid: str) -> Path:
            """Return the procedural sandbox for one node identifier.

            :param _nodeid: Synthetic pytest node identifier.
            """

            return sandbox

    class Item:
        """Provide the pytest item attributes used by retention handling."""

        class Config:
            """Expose the sandbox-retention pytest option."""

            @staticmethod
            def getoption(name: str) -> bool:
                """Return the requested synthetic option value.

                :param name: Pytest option name being queried.
                """

                assert name == "keep_sandboxes"
                return keep_sandboxes

        config = Config()
        nodeid = "uranium_tests/example/test_example.py::test_example"

        @staticmethod
        def get_closest_marker(name: str) -> pytest.Mark | None:
            """Return the procedural marker requested by the hook.

            :param name: Marker name being queried.
            """

            return pytest.mark.uranium_procedural.mark if name == "uranium_procedural" else None

    monkeypatch.setattr("tools.uranium.plugin.get_runtime", lambda _config: Runtime())
    item = Item()
    call = type("Report", (), {"failed": did_fail, "when": "call"})()
    teardown = type("Report", (), {"failed": False, "when": "teardown"})()

    _update_sandbox_retention(item, call)  # type: ignore[arg-type]
    _update_sandbox_retention(item, teardown)  # type: ignore[arg-type]

    assert sandbox.exists() is is_retained
