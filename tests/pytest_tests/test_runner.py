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

from uranium_testkit.runner import (
    RunnerError,
    _cmake_cache_value,
    _copy_sandbox_artifacts,
    _short_sandbox,
    choose_docker_mode,
    is_official_test_container,
    launch_docker,
    translate_arguments,
)


def test_explicit_container_mode_is_removed_from_test_arguments() -> None:
    """Do not leak container-only flags into pytest."""

    assert choose_docker_mode(["--run-in-docker", "-k", "cache"]) == (True, ["-k", "cache"])
    assert choose_docker_mode(["--no-run-in-docker", "-k", "cache"]) == (False, ["-k", "cache"])


def test_conflicting_container_modes_fail() -> None:
    """Reject an ambiguous explicit execution environment."""

    with pytest.raises(RunnerError, match="conflicts"):
        choose_docker_mode(["--run-in-docker", "--no-run-in-docker"])


def test_official_container_requires_fedora_44_and_container_marker(tmp_path: Path) -> None:
    """Do not mistake a Fedora host or a different container image for CI."""

    os_release = tmp_path / "etc" / "os-release"
    os_release.parent.mkdir()
    os_release.write_text('ID="fedora"\nVERSION_ID="44"\n')
    assert not is_official_test_container(tmp_path)

    (tmp_path / ".dockerenv").touch()
    assert is_official_test_container(tmp_path)

    os_release.write_text('ID="fedora"\nVERSION_ID="45"\n')
    assert not is_official_test_container(tmp_path)


def test_urtest_arguments_translate_to_pytest() -> None:
    """Preserve the common established command-line spellings."""

    translated = translate_arguments(
        ["-j2", "-v", "--sandbox", "/tmp/sb", "--clean=none", "-f", "cache-*", "tls-basic"],
        {},
    )
    assert translated == [
        "-n",
        "2",
        "--dist",
        "loadgroup",
        "-v",
        "--sandbox",
        "/tmp/sb",
        "--legacy-clean",
        "none",
        "--urtest-filter",
        "cache-*",
        "--urtest-filter",
        "tls-basic",
    ]


def test_ci_sharding_replaces_old_python_only_filter_list() -> None:
    """Let pytest shard its full inventory when old Jenkins passes Python names."""

    translated = translate_arguments(["-f", "legacy-one", "legacy-two"], {"SHARD": "3", "SHARDCNT": "12"})
    assert translated == ["--urtest-shard-index", "3", "--urtest-shard-count", "12"]


def test_invalid_ci_shard_fails() -> None:
    """Prevent a typo from silently collecting no tests."""

    with pytest.raises(RunnerError, match="SHARD"):
        translate_arguments([], {"SHARD": "12", "SHARDCNT": "12"})


def test_docker_launch_forwards_ci_sharding(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Keep the stable pytest shard when the source launcher enters Docker."""

    commands: list[list[str]] = []

    def record_run(command: list[str], *, check: bool) -> subprocess.CompletedProcess[str]:
        assert not check
        commands.append(command)
        return subprocess.CompletedProcess(command, 0)

    def find_executable(executable: str) -> str:
        return f"/usr/bin/{executable}"

    monkeypatch.setattr("uranium_testkit.runner.shutil.which", find_executable)
    monkeypatch.setattr("uranium_testkit.runner.subprocess.run", record_run)
    monkeypatch.setenv("SHARD", "3")
    monkeypatch.setenv("SHARDCNT", "12")

    assert launch_docker(tmp_path, ["-j2", "-q"]) == 0
    command = commands[0]
    assert command[command.index("SHARD=3") - 1] == "--env"
    assert command[command.index("SHARDCNT=12") - 1] == "--env"
    assert command[-4:] == [str(tmp_path / "tests" / "urtest.sh"), "--no-run-in-docker", "-j2", "-q"]


def test_source_runner_keeps_a_short_stable_sandbox(tmp_path: Path) -> None:
    """Leave enough path space for ATS and AuTest Unix sockets."""

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
