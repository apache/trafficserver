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
"""Unit tests for the unified ATS test command."""

from pathlib import Path
import os
import subprocess

import pytest

from tools.uranium.runner import (
    RunnerError,
    _cmake_cache_value,
    _copy_sandbox_artifacts,
    _short_sandbox,
    _uv_run_prefix,
    choose_container_mode,
    find_container_runtime,
    is_official_test_container,
    launch_container,
    runner_help,
    translate_arguments,
)


def test_explicit_container_mode_is_removed_from_test_arguments() -> None:
    """Do not leak container-only flags into pytest."""

    assert choose_container_mode(["--run-in-container", "-k", "cache"]) == (True, ["-k", "cache"])
    assert choose_container_mode(["--no-run-in-container", "-k", "cache"]) == (False, ["-k", "cache"])
    assert choose_container_mode(["--run-in-docker", "-k", "cache"]) == (True, ["-k", "cache"])
    assert choose_container_mode(["--no-run-in-docker", "-k", "cache"]) == (False, ["-k", "cache"])


def test_conflicting_container_modes_fail() -> None:
    """Reject an ambiguous explicit execution environment."""

    with pytest.raises(RunnerError, match="conflicts"):
        choose_container_mode(["--run-in-container", "--no-run-in-docker"])


def test_official_environment_recognizes_fedora_44_without_runtime_marker(tmp_path: Path) -> None:
    """Recognize Apple containers, which do not add an OCI marker.

    :param tmp_path: Stand-in filesystem root for the Fedora release file.
    """

    os_release = tmp_path / "etc" / "os-release"
    os_release.parent.mkdir()
    os_release.write_text('ID="fedora"\nVERSION_ID="44"\n')
    assert is_official_test_container(tmp_path)

    os_release.write_text('ID="fedora"\nVERSION_ID="45"\n')
    assert not is_official_test_container(tmp_path)


def test_any_container_runs_directly_by_default(tmp_path: Path) -> None:
    """Avoid nested container execution even outside the official image.

    :param tmp_path: Stand-in filesystem root for container markers.
    """

    (tmp_path / ".dockerenv").touch()
    assert choose_container_mode(["-q"], tmp_path) == (False, ["-q"])


def test_fedora_44_runs_directly_without_a_runtime_marker(tmp_path: Path) -> None:
    """Avoid nesting from an Apple container running the official image.

    :param tmp_path: Stand-in filesystem root for the Fedora release file.
    """

    os_release = tmp_path / "etc" / "os-release"
    os_release.parent.mkdir()
    os_release.write_text('ID="fedora"\nVERSION_ID="44"\n')

    assert choose_container_mode(["-q"], tmp_path) == (False, ["-q"])


def test_pytest_arguments_pass_through_unchanged() -> None:
    """Leave selection, parallelism, collection, and sandbox options to pytest."""

    arguments = ["-n", "2", "-v", "--sandbox", "/tmp/sb", "--collect-only", "-k", "cache or tls"]

    assert translate_arguments(arguments, {}) == arguments


def test_fedora_runner_installs_the_accelerated_diff_extra(tmp_path: Path) -> None:
    """Request cdifflib only in the supported Fedora environment.

    :param tmp_path: Stand-in path for the configured Python project.
    """

    assert _uv_run_prefix(tmp_path, True) == ["uv", "--project", str(tmp_path), "run", "--extra", "fast-diff"]
    assert _uv_run_prefix(tmp_path, False) == ["uv", "--project", str(tmp_path), "run"]


def test_runner_help_separates_wrapper_and_pytest_options() -> None:
    """Explain the wrapper's priority before displaying downstream help."""

    help_text = runner_help()

    assert "urtest.sh options (consumed before pytest; these spellings take precedence):" in help_text
    assert "-k for selection, -n for parallel workers" in help_text
    assert "--collect-only/--co for listing tests" in help_text
    assert "--run-manual to include explicitly" in help_text
    assert "-j N" not in help_text
    assert "--clean" not in help_text
    assert "--list" not in help_text
    assert "Apple container on macOS, Podman on Linux, and Docker as a fallback" in help_text
    assert help_text.endswith("pytest options (passed through after urtest.sh processing):")


def test_ci_sharding_preserves_pytest_selection() -> None:
    """Combine native pytest selection with stable CI sharding."""

    translated = translate_arguments(["-k", "legacy-one or legacy-two"], {"SHARD": "3", "SHARDCNT": "12"})
    assert translated == [
        "-k",
        "legacy-one or legacy-two",
        "--urtest-shard-index",
        "3",
        "--urtest-shard-count",
        "12",
    ]


def test_pytest_short_options_are_not_reinterpreted() -> None:
    """Leave pytest and plugin short options under pytest's control."""

    assert translate_arguments(["-f", "-k", "basic"], {}) == ["-f", "-k", "basic"]


def test_invalid_ci_shard_fails() -> None:
    """Prevent a typo from silently collecting no tests."""

    with pytest.raises(RunnerError, match="SHARD"):
        translate_arguments([], {"SHARD": "12", "SHARDCNT": "12"})


def test_runtime_selection_prefers_host_native_tools(monkeypatch: pytest.MonkeyPatch) -> None:
    """Prefer Apple container on macOS and Podman on Linux.

    :param monkeypatch: Pytest environment and function patch helper.
    """

    available = {"container": "/usr/bin/container", "podman": "/usr/bin/podman", "docker": "/usr/bin/docker"}
    monkeypatch.setattr("tools.uranium.runner.shutil.which", available.get)

    assert find_container_runtime("darwin") == ("container", "/usr/bin/container")
    assert find_container_runtime("linux") == ("podman", "/usr/bin/podman")


def test_container_launch_forwards_ci_sharding(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Keep the stable pytest shard when the source launcher enters Podman.

    :param monkeypatch: Pytest environment and function patch helper.
    :param tmp_path: Stand-in ATS source-tree root.
    """

    commands: list[list[str]] = []

    def record_run(command: list[str], *, check: bool) -> subprocess.CompletedProcess[str]:
        """Record a simulated Podman invocation.

        :param command: Complete runtime command to record.
        :param check: Whether subprocess failures should raise an exception.
        """

        assert not check
        commands.append(command)
        return subprocess.CompletedProcess(command, 0)

    def find_executable(executable: str) -> str | None:
        """Expose only Podman to runtime discovery.

        :param executable: Runtime executable name to resolve.
        """

        return f"/usr/bin/{executable}" if executable == "podman" else None

    monkeypatch.setattr("tools.uranium.runner.shutil.which", find_executable)
    monkeypatch.setattr("tools.uranium.runner.subprocess.run", record_run)
    monkeypatch.setenv("SHARD", "3")
    monkeypatch.setenv("SHARDCNT", "12")

    monkeypatch.setattr("tools.uranium.runner.sys.platform", "linux")

    assert launch_container(tmp_path, ["-n2", "-q"]) == 0
    command = commands[0]
    assert command[0] == "/usr/bin/podman"
    assert "--network=host" in command
    assert "ATS_URTEST_IN_CONTAINER=1" in command
    assert command[command.index("SHARD=3") - 1] == "--env"
    assert command[command.index("SHARDCNT=12") - 1] == "--env"
    assert command[-4:] == [str(tmp_path / "tests" / "urtest.sh"), "--no-run-in-container", "-n2", "-q"]


def test_apple_container_launch_omits_unsupported_host_network(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Use Apple container without requesting Docker-style host networking.

    :param monkeypatch: Pytest environment and function patch helper.
    :param tmp_path: Stand-in ATS source-tree root.
    """

    commands: list[list[str]] = []

    def record_run(command: list[str], *, check: bool) -> subprocess.CompletedProcess[str]:
        """Record a simulated Apple container invocation.

        :param command: Complete runtime command to record.
        :param check: Whether subprocess failures should raise an exception.
        """

        assert not check
        commands.append(command)
        return subprocess.CompletedProcess(command, 0)

    def find_executable(executable: str) -> str | None:
        """Expose only Apple container to runtime discovery.

        :param executable: Runtime executable name to resolve.
        """

        return "/usr/bin/container" if executable == "container" else None

    monkeypatch.setattr("tools.uranium.runner.shutil.which", find_executable)
    monkeypatch.setattr("tools.uranium.runner.subprocess.run", record_run)
    monkeypatch.setattr("tools.uranium.runner.sys.platform", "darwin")

    assert launch_container(tmp_path, ["-q"]) == 0
    assert commands[0][0] == "/usr/bin/container"
    assert "--network=host" not in commands[0]


def test_source_runner_keeps_a_short_stable_sandbox(tmp_path: Path) -> None:
    """Leave enough path space for ATS Unix sockets."""

    assert _short_sandbox(tmp_path) == _short_sandbox(tmp_path)
    assert _short_sandbox(tmp_path).parent == Path("/tmp")
    assert len(str(_short_sandbox(tmp_path))) < 32


def test_cmake_cache_value_detects_a_required_reconfigure(tmp_path: Path) -> None:
    """Update the configured wrapper when its short sandbox changes."""

    cache = tmp_path / "CMakeCache.txt"
    cache.write_text("IGNORED:BOOL=ON\nURTEST_SANDBOX:STRING=/tmp/ats-urtest-12345678\n")
    assert _cmake_cache_value(cache, "URTEST_SANDBOX") == "/tmp/ats-urtest-12345678"
    assert _cmake_cache_value(cache, "MISSING") is None


def test_sandbox_copy_omits_container_special_files(tmp_path: Path) -> None:
    """Preserve diagnostics without copying Unix sockets or runroot symlinks."""

    source = tmp_path / "source"
    destination = tmp_path / "destination"
    source.mkdir()
    (source / "diags.log").write_text("diagnostic")
    (source / "binary").symlink_to("/container/install/traffic_server")
    os.mkfifo(source / "manager.fifo")
    _copy_sandbox_artifacts(source, destination)

    assert (destination / "diags.log").read_text() == "diagnostic"
    assert not (destination / "binary").is_symlink()
    assert not (destination / "manager.fifo").exists()
