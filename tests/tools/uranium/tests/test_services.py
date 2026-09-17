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
import socket
import errno
from typing import Any

import pytest

from tools.uranium import services as service_api
from tools.uranium.runtime import TestRuntime as UraniumRuntime
from tools.uranium.runtime import RuntimeConfigError
from tools.uranium.services import ATS, ATSFactory, Curl, ProceduralContext, ServiceFactory
from tools.uranium.plugin import uranium_replay
from tools.uranium.replay import ReplayTest


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

    def output(self) -> str:
        """Return a diagnostic marker for exit-status tests."""

        return "captured process output"


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
    ats.log_directory.mkdir(parents=True, exist_ok=True)
    ats.diags_log.write_text("ATS initialized\n")
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


def test_sandbox_rejects_overlong_filesystem_component(tmp_path: Path) -> None:
    """Reject unrepresentable labels instead of silently shortening them.

    :param tmp_path: Existing sandbox root on the test filesystem.
    """

    runtime = UraniumRuntime(tmp_path, tmp_path, tmp_path, tmp_path, tmp_path, {}, {})
    path = tmp_path / ("x" * (os.pathconf(tmp_path, "PC_NAME_MAX") + 1))
    with pytest.raises(RuntimeConfigError, match="filesystem component limit"):
        runtime.prepare_sandbox(path)


@pytest.mark.parametrize("content", [None, "", "garbage", "9999", "30000"])
def test_port_counter_rejects_missing_corrupt_or_exhausted_state(tmp_path: Path, content: str | None) -> None:
    """Never recycle reservations when the session counter is damaged.

    :param tmp_path: Isolated session root.
    :param content: Counter contents, or None to leave the file absent.
    """

    runtime = UraniumRuntime(tmp_path, tmp_path, tmp_path, tmp_path, tmp_path, {}, {})
    counter = tmp_path / ".port-counter"
    if content is not None:
        counter.write_text(content)
    with pytest.raises(RuntimeConfigError, match=str(counter)):
        runtime.allocate_port()


@pytest.mark.parametrize("error_number", [errno.EADDRINUSE, errno.EADDRNOTAVAIL, errno.ENOBUFS])
def test_port_probe_retries_only_occupied_ports(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, error_number: int) -> None:
    """Report environmental errors instead of treating them as occupied ports.

    :param tmp_path: Isolated session root.
    :param monkeypatch: Replace socket binding with a deterministic failure.
    :param error_number: Operating-system error raised by the first bind.
    """

    runtime = UraniumRuntime(tmp_path, tmp_path, tmp_path, tmp_path, tmp_path, {}, {})
    (tmp_path / ".port-counter").write_text("10000")
    attempted = []

    def bind(probe: socket.socket, address: tuple[str, int]) -> None:
        """Fail the initial probe without binding an actual port.

        :param probe: Unbound socket under test.
        :param address: Requested loopback address and port.
        """

        attempted.append(address)
        if len(attempted) == 1:
            raise OSError(error_number, os.strerror(error_number))

    monkeypatch.setattr(socket.socket, "bind", bind)
    if error_number == errno.EADDRINUSE:
        assert runtime.allocate_port() == 10002
    else:
        with pytest.raises(RuntimeConfigError, match="127.0.0.1:10001"):
            runtime.allocate_port()
        assert len(attempted) == 1


def test_embedded_replay_preserves_services_and_variants(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Give each embedded replay its own directory under the native test.

    :param tmp_path: Isolated session root.
    :param monkeypatch: Replace network execution with evidence creation.
    """

    runtime = UraniumRuntime(tmp_path, tmp_path, tmp_path, tmp_path, tmp_path, {}, {})
    (tmp_path / ".port-counter").write_text("10000")
    manifest = tmp_path / "example.yaml"
    manifest.write_text(
        "urtest:\n  description: example\n  server: {}\n  client: {}\n  ats: {}\n"
        "  variants:\n    - name: one\n    - name: two\n")
    context = ProceduralContext.create(runtime, "native-test", manifest)
    live = context.run_directory / "live-service"
    live.mkdir()
    (live / "log").write_text("do not delete")

    def run(replay: ReplayTest) -> None:
        """Materialize the replay sandbox without starting network services.

        :param replay: Replay carrying the fixture's ownership information.
        """

        runtime.prepare_sandbox(replay.sandbox, parent=replay._sandbox_parent)
        (replay.sandbox / "result").write_text(replay.spec.variant_name)

    monkeypatch.setattr(ReplayTest, "run", run)
    invoke = uranium_replay.__wrapped__(context)
    invoke(manifest)
    invoke(manifest)
    assert (live / "log").read_text() == "do not delete"
    assert sorted(path.read_text() for path in context.run_directory.glob("replay-*/result")) == ["one", "one", "two", "two"]


def test_long_names_allow_both_unix_sockets(tmp_path: Path) -> None:
    """Bind the actual composed RPC and curl socket paths beneath long names.

    :param tmp_path: Parent of a deliberately long sandbox path.
    """

    root = tmp_path / ("long-test-name-" * 9)
    root.mkdir()
    ats = ATS(make_context(root), "long-process-name-" * 4)
    installation = tmp_path / "installation"
    installation.mkdir()
    ats._context.runtime.layout = {"BINDIR": str(installation), "PLUGINDIR": str(installation), "SYSCONFDIR": str(installation)}
    ats._context.runtime.repository_root = Path(__file__).parents[4]
    try:
        paths = ats._runner._prepare_ats_tree(ats.run_directory)
        for path in (paths["rpc_runtime"] / "jsonrpc20.sock", Path(ats.uds_path)):
            assert len(os.fsencode(path)) < 104
            with socket.socket(socket.AF_UNIX) as listener:
                listener.bind(str(path))
    finally:
        ats.close()


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
    second.log_directory.mkdir(parents=True, exist_ok=True)
    second.diags_log.write_text("FATAL: validation failed\n")

    with pytest.raises(ExceptionGroup, match="cleanup failed"):
        factory.close()

    assert first_process.was_stopped
    assert second_process.was_stopped


@pytest.mark.parametrize("status", [0, 1, -11, -6])
def test_ats_rejects_unexpected_exit(tmp_path: Path, status: int) -> None:
    """A completed client or positive log marker must not hide an ATS exit.

    :param tmp_path: Temporary test tree.
    :param status: Premature exit status, including clean exits and signals.
    """

    ats = ATS(make_context(tmp_path))
    process = attach_process(ats)
    ats.start()
    process.return_code = status
    with pytest.raises(AssertionError, match="exited unexpectedly"):
        ats.close()


def test_ats_requires_diagnostic_log(tmp_path: Path) -> None:
    """A missing log is missing evidence, not a successful diagnostic check.

    :param tmp_path: Temporary test tree.
    """

    ats = ATS(make_context(tmp_path))
    attach_process(ats)
    ats.start()
    ats.diags_log.unlink()
    with pytest.raises(AssertionError, match="Missing ATS diagnostic log"):
        ats.close()


@pytest.mark.parametrize("destination", ["custom.log", "stdout", "stderr"])
@pytest.mark.parametrize("capture", [True, False])
def test_ats_checks_configured_diagnostic_destination(tmp_path: Path, destination: str, capture: bool) -> None:
    """Renamed logs and redirected streams retain the fatal-diagnostic check.

    :param tmp_path: Temporary test tree.
    :param destination: Configured file name or standard stream.
    :param capture: Whether ATS redirects standard streams to traffic.out.
    """

    ats = ATS(make_context(tmp_path), capture_traffic_out=capture)
    ats.records.update({"proxy.config.diags.logfile.filename": destination})
    attach_process(ats)
    ats.start()
    ats.diags_log.write_text("FATAL: diagnostic in the configured destination\n")
    with pytest.raises(AssertionError, match="emitted a fatal diagnostic"):
        ats.close()


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
