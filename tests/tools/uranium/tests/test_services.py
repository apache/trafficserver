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

from importlib import import_module
from pathlib import Path
import json
import os
import subprocess
from typing import Any

import pytest

from tools.uranium import services as service_api
from tools.uranium.runtime import TestRuntime as UraniumRuntime
from tools.uranium.services import ATS, ATSFactory, Curl, ProceduralContext, ServiceFactory


class FakeRuntime:
    """Allocate deterministic ports without requiring an ATS installation."""

    def __init__(self, root: Path) -> None:
        self.repository_root = root
        self.features: dict[str, bool] = {}
        self._port = 12344

    def allocate_port(self, *_args: Any) -> int:
        self._port += 1
        return self._port

    def item_sandbox(self, *_args: Any) -> Path:
        return self.repository_root / "unused"


class FakeProcess:
    """Record native managed-process lifecycle calls."""

    def __init__(self) -> None:
        self.return_code: int | None = None
        self.was_stopped = False

    def stop(self) -> None:
        self.was_stopped = True
        self.return_code = 0


def make_context(tmp_path: Path) -> ProceduralContext:
    """Create a native procedural context for service unit tests."""

    test_path = tmp_path / "test_example.py"
    test_path.touch()
    run_directory = tmp_path / "run"
    run_directory.mkdir()
    return ProceduralContext(FakeRuntime(tmp_path), "test_example", test_path, run_directory)  # type: ignore[arg-type]


def attach_process(ats: ATS) -> FakeProcess:
    """Replace ATS startup with a lifecycle-recording fake process."""

    process = FakeProcess()
    ats._runner._start_ats = lambda: process  # type: ignore[method-assign,return-value]
    return process


def test_services_facade_reexports_focused_implementations() -> None:
    """Keep the existing scenario import surface stable after the split."""

    implementations = {
        "ATS": "tools.uranium.services.ats",
        "ATSFactory": "tools.uranium.services.ats",
        "CommandResult": "tools.uranium.services.context",
        "ConfigFile": "tools.uranium.services.ats",
        "Curl": "tools.uranium.services.curl",
        "DNSServer": "tools.uranium.services.dns",
        "HttpBinServer": "tools.uranium.services.httpbin",
        "OriginServer": "tools.uranium.services.origin",
        "ProceduralContext": "tools.uranium.services.context",
        "ProcessService": "tools.uranium.services.process_service",
        "RecordsConfig": "tools.uranium.services.ats",
        "ServiceFactory": "tools.uranium.services.service_factory",
        "VerifierServer": "tools.uranium.services.verifier",
        "assert_matches_gold": "tools.uranium.assertions",
        "send_tcp": "tools.uranium.services.service_utils",
        "wait_for_file_lines": "tools.uranium.services.service_utils",
        "wait_for_metric": "tools.uranium.services.service_utils",
    }

    assert set(service_api.__all__) == set(implementations)
    for name, module_name in implementations.items():
        assert getattr(service_api, name) is getattr(import_module(module_name), name)


def test_procedural_sandbox_preserves_name_and_cleans_previous_run(tmp_path: Path) -> None:
    """Reuse the complete test name and discard stale artifacts on rerun.

    :param tmp_path: Temporary root used to construct a synthetic runtime.
    """

    runtime = UraniumRuntime(
        repository_root=tmp_path,
        build_root=tmp_path,
        ats_bin=tmp_path,
        verifier_bin=tmp_path,
        sandbox_root=tmp_path,
        layout={},
        features={},
    )
    sandbox = runtime.procedural_sandbox(
        "uranium_tests/cache/test_cache_shm_control_size_mismatch.py::test_cache_shm_control_size_mismatch")
    assert sandbox.name == "test_cache_shm_control_size_mismatch"
    runtime.prepare_sandbox(sandbox)
    (sandbox / "previous-run.log").write_text("old output")
    unrelated = tmp_path / "another-test"
    unrelated.mkdir()
    runtime.prepare_sandbox(sandbox)
    assert list(sandbox.iterdir()) == []
    assert unrelated.is_dir()


def test_replay_sandbox_identifies_test_and_avoids_name_collisions(tmp_path: Path) -> None:
    """Put readable, unique replay directories directly below the root.

    :param tmp_path: Temporary repository root used by the synthetic runtime.
    """

    runtime = UraniumRuntime(
        repository_root=tmp_path,
        build_root=tmp_path,
        ats_bin=tmp_path,
        verifier_bin=tmp_path,
        sandbox_root=Path("/tmp/ats-urtest-12345678"),
        layout={},
        features={},
    )
    replay = tmp_path / "tests" / "uranium_tests" / "cache" / "cache.test.yaml"
    first = runtime.item_sandbox(replay, "uranium_tests/cache/cache.test.yaml::cache-default")
    second = runtime.item_sandbox(replay, "uranium_tests/cache/cache.test.yaml::cache-tls")

    assert first.parent == runtime.sandbox_root
    assert first.name == "cache-default"
    assert second.name == "cache-tls"
    assert first != second


def test_long_sandbox_uses_short_uds_path(tmp_path: Path) -> None:
    """Keep Unix listeners usable under full-length sandbox names.

    :param tmp_path: Temporary root for a deeply nested sandbox.
    """

    long_root = tmp_path / ("long-sandbox-name-" * 8)
    long_root.mkdir()
    ats = ATS(make_context(long_root))
    try:
        socket_path = Path(ats.uds_path)
        assert len(os.fsencode(socket_path)) < 104
        assert ats.uds_path == str(socket_path)
        assert socket_path.parent.is_dir()
    finally:
        ats.close()
    assert not socket_path.parent.exists()


def test_sandbox_names_preserve_parameter_ids() -> None:
    """Keep parameterized cases distinct without adding generated suffixes."""

    assert UraniumRuntime.sandbox_name("test_tls.py::test_timeout[get-handshake]") == "test_timeout[get-handshake]"
    assert UraniumRuntime.sandbox_name("test_tls.py::test_timeout[post-handshake]") == "test_timeout[post-handshake]"


def test_ats_owns_process_lifecycle(tmp_path: Path) -> None:
    """Start, stop, and validate the fixture-owned ATS process."""

    ats = ATS(make_context(tmp_path))
    process = attach_process(ats)

    assert ats.http_port == 12345
    assert ats.uds_path.endswith("/run/ats/runtime/ats.sock")
    assert not ats.is_running

    ats.records.update({"proxy.config.http.server_ports": "12345"})

    ats.start()
    assert ats.is_running
    assert ats._runner.spec.urtest["ats"]["records_config"] == {"proxy.config.http.server_ports": "12345"}

    ats.close()
    assert process.was_stopped


def test_ats_factory_owns_multiple_process_lifecycles(tmp_path: Path) -> None:
    """Create, stop, and validate multiple independent Traffic Servers."""

    factory = ATSFactory(make_context(tmp_path))

    first = factory.create("first")
    second = factory.create("second", enable_cache=False)
    first_process = attach_process(first)
    second_process = attach_process(second)

    assert first.http_port == 12345
    assert second.http_port > first.http_port

    first.start()
    second.start()
    assert second._runner.spec.urtest["ats"]["process_config"] == {"enable_cache": False}
    factory.close()

    assert first_process.was_stopped
    assert second_process.was_stopped


def test_ats_factory_rejects_duplicate_names(tmp_path: Path) -> None:
    """Keep each public service handle bound to a distinct process."""

    factory = ATSFactory(make_context(tmp_path))

    factory.create("ats")

    with pytest.raises(ValueError, match="already exists"):
        factory.create("ats")


def test_ats_factory_closes_all_processes_after_validation_failure(tmp_path: Path) -> None:
    """Do not leak one Traffic Server when another fails during teardown."""

    factory = ATSFactory(make_context(tmp_path))
    first = factory.create("first")
    second = factory.create("second")
    first_process = attach_process(first)
    second_process = attach_process(second)
    first.start()
    second.start()
    second.log_directory.mkdir(parents=True)
    second.diags_log.write_text("FATAL: validation failed\n")

    with pytest.raises(ExceptionGroup, match="cleanup failed"):
        factory.close()

    assert first_process.was_stopped
    assert second_process.was_stopped


def test_origin_healthcheck_has_a_distinct_header_lookup_key(tmp_path: Path) -> None:
    """Keep the internal response from claiming a test's missing-header key.

    :param tmp_path: Temporary directory containing the generated replay data.
    """

    factory = ServiceFactory(make_context(tmp_path))

    factory.origin("origin", lookup_key="{%uuid}")

    healthcheck = tmp_path / "run/origin/data/healthcheck.json"
    document = json.loads(healthcheck.read_text())
    fields = document["sessions"][0]["transactions"][0]["client-request"]["headers"]["fields"]
    assert ["uuid", "uranium-healthcheck"] in fields


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
    """Expose curl status and output for ordinary pytest assertions.

    :param monkeypatch: Pytest fixture used to observe the subprocess command.
    :param tmp_path: Temporary working directory for the Curl instance.
    :param use_uds: Whether Curl should target ATS through a Unix socket.
    :param transport_arguments: Expected curl arguments for the selected
        transport.
    """

    observed: dict[str, Any] = {}

    def run(command: tuple[str, ...], **kwargs: Any) -> subprocess.CompletedProcess[str]:
        """Record the curl invocation and return a synthetic failure.

        :param command: Tokenized command passed to subprocess.
        :param kwargs: Subprocess execution options.
        """

        observed["command"] = command
        observed.update(kwargs)
        return subprocess.CompletedProcess(command, 7, "response", "diagnostic")

    monkeypatch.setattr("tools.uranium.services.curl.subprocess.run", run)
    ats = ATS(make_context(tmp_path))

    result = Curl(tmp_path, use_uds=use_uds).get(ats, "status", headers={"X-Test": "value"})

    expected_transport = ("--unix-socket", ats.uds_path) if use_uds else ()
    assert observed["command"] == (
        "curl",
        *expected_transport,
        "--header",
        "X-Test: value",
        f"http://127.0.0.1:{ats.http_port}/status",
    )
    assert observed["cwd"] == tmp_path
    assert result.returncode == 7
    assert result.output == "responsediagnostic"


def test_curl_parses_a_shell_style_argument_string(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Preserve quoted curl values without invoking a shell.

    :param monkeypatch: Pytest fixture used to observe the subprocess command.
    :param tmp_path: Temporary working directory for the Curl instance.
    """

    observed: dict[str, Any] = {}

    def run(command: tuple[str, ...], **kwargs: Any) -> subprocess.CompletedProcess[str]:
        """Record the parsed curl argument vector.

        :param command: Tokenized command passed to subprocess.
        :param kwargs: Subprocess execution options.
        """

        observed["command"] = command
        observed.update(kwargs)
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr("tools.uranium.services.curl.subprocess.run", run)

    result = Curl(tmp_path).run("--verbose --header 'X-Test: one two' http://example.test/path")

    assert result.returncode == 0
    assert observed["command"] == (
        "curl",
        "--verbose",
        "--header",
        "X-Test: one two",
        "http://example.test/path",
    )
    assert observed["shell"] is False
