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

from pathlib import Path
import os
import subprocess

import pytest

from tools.uranium.services import ProceduralContext


class RunrootScenario:
    """Exercise traffic_layout runroot creation, selection, verification, and removal."""

    def __init__(self, context: ProceduralContext) -> None:
        self._context = context
        self._directory = context.run_directory
        self._traffic_layout = context.runtime.ats_bin / "traffic_layout"
        self._layout = context.runtime.layout

    def run(
            self,
            *arguments: str | Path,
            cwd: Path | None = None,
            environment: dict[str, str] | None = None,
            expected_return_codes: tuple[int, ...] = (0,),
    ) -> subprocess.CompletedProcess[str]:
        """Run traffic_layout and return its captured output."""

        result = subprocess.run(
            [self._traffic_layout, *(str(argument) for argument in arguments)],
            cwd=cwd or self._directory,
            env=environment,
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        assert result.returncode in expected_return_codes, result.stdout + result.stderr
        return result

    def init(self, path: Path, *, force: bool = False, cwd: Path | None = None) -> None:
        """Create one runroot and verify its metadata file."""

        arguments = ["init"]
        if force:
            arguments.append("--force")
        arguments.extend(["--path", path if path.is_absolute() else path.name])
        self.run(*arguments, cwd=cwd)
        resolved = path if path.is_absolute() else (cwd or self._directory) / path
        assert (resolved / "runroot.yaml").is_file()

    def require_prefix_layout(self) -> tuple[str, str]:
        """Return prefix-relative bin and log directories or skip."""

        prefix = self._layout["PREFIX"]
        bindir = self._layout["BINDIR"]
        logdir = self._layout["LOGDIR"]
        if not bindir.startswith(prefix):
            pytest.skip("traffic_layout BINDIR must be below PREFIX")
        bin_suffix = os.path.relpath(bindir, prefix)
        log_suffix = os.path.relpath(logdir, prefix) if logdir.startswith(prefix) else logdir.lstrip("/")
        return bin_suffix, log_suffix

    def run_error_cases(self) -> None:
        """Verify diagnostics for existing, nested, and invalid runroots."""

        path = self._directory / "runroot"
        self.init(path)
        result = self.run("init", "--path", path)
        assert "Using existing runroot" in result.stdout + result.stderr

        nested = path / "runroot"
        result = self.run("init", "--path", nested, expected_return_codes=(70,))
        assert "Cannot create runroot inside another runroot" in result.stdout + result.stderr
        assert not (nested / "runroot.yaml").exists()

        invalid = self._directory / "missing"
        result = self.run("remove", "--path", invalid, expected_return_codes=(0, 70))
        assert "Unable to read" in result.stdout + result.stderr
        result = self.run("verify", "--path", invalid, expected_return_codes=(0, 70))
        assert "Unable to read" in result.stdout + result.stderr

    def run_init_cases(self) -> None:
        """Verify absolute, relative, current-directory, forced, and copied initialization."""

        bin_suffix, _ = self.require_prefix_layout()
        first = self._directory / "runroot1"
        self.init(first)
        self.init(Path("runroot2"), cwd=self._directory)

        third = self._directory / "runroot3"
        third.mkdir()
        self.run("init", cwd=third)
        assert (third / "runroot.yaml").is_file()

        fourth = self._directory / "runroot4"
        fourth.mkdir()
        (fourth / "foo").touch()
        self.init(fourth, force=True)

        junk = first / bin_suffix / "junk"
        junk.touch()
        fifth = self._directory / "runroot5"
        copied_layout = first / bin_suffix / "traffic_layout"
        result = subprocess.run(
            [copied_layout, "init", "--path", fifth],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        assert (fifth / "runroot.yaml").is_file()
        assert junk.is_file()
        assert not (fifth / bin_suffix / "junk").exists()

    def run_remove_cases(self) -> None:
        """Verify removal by absolute path, relative path, and current directory."""

        paths = [self._directory / f"runroot{number}" for number in range(1, 4)]
        for path in paths:
            self.init(path)
        self.run("remove", "--path", paths[0])
        assert not paths[0].exists()
        self.run("remove", "--path", paths[1].name, cwd=self._directory)
        assert not paths[1].exists()
        self.run("remove", cwd=paths[2])
        assert paths[2].is_dir()
        assert not (paths[2] / "runroot.yaml").exists()

    def run_use_cases(self) -> None:
        """Verify explicit, cwd, executable, and environment runroot discovery."""

        bin_suffix, _ = self.require_prefix_layout()
        first = self._directory / "runroot1"
        second = self._directory / "runroot2"
        self.init(first)
        self.init(second)
        assert f"PREFIX: {first}" in self.run("info", f"--run-root={first}").stdout
        assert f"PREFIX: {first}" in self.run("info", cwd=first).stdout

        copied_layout = first / bin_suffix / "traffic_layout"
        result = subprocess.run([copied_layout, "info"], capture_output=True, text=True, timeout=60, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        assert f"PREFIX: {first}" in result.stdout

        environment = os.environ.copy()
        environment["TS_RUNROOT"] = str(second)
        assert f"PREFIX: {second}" in self.run("info", environment=environment).stdout

    def run_verify_cases(self) -> None:
        """Verify a runroot through both installed and copied executables."""

        bin_suffix, log_suffix = self.require_prefix_layout()
        path = self._directory / "runroot"
        self.init(path)
        runroot_yaml = path / "runroot.yaml"
        runroot_yaml.write_text(
            runroot_yaml.read_text().replace(
                f"runtimedir: {self._layout['RUNTIMEDIR']}",
                "runtimedir: ./var/trafficserver",
            ))
        for directory in (path / "var/trafficserver", path / "var/log/trafficserver"):
            directory.chmod(0o777)
        first = self.run("verify", "--path", path).stdout
        for expected in (str(path / bin_suffix), str(path / log_suffix), "PASSED"):
            assert expected in first

        copied_layout = path / bin_suffix / "traffic_layout"
        result = subprocess.run(
            [copied_layout, "verify", "--path", path],
            cwd=path,
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        for expected in (str(path / bin_suffix), str(path / log_suffix), "PASSED"):
            assert expected in result.stdout


def test_runroot_errors(procedural_context: ProceduralContext) -> None:
    """Invalid runroot operations report the expected diagnostics."""

    RunrootScenario(procedural_context).run_error_cases()


def test_runroot_init(procedural_context: ProceduralContext) -> None:
    """traffic_layout initializes runroots in all supported forms."""

    RunrootScenario(procedural_context).run_init_cases()


def test_runroot_remove(procedural_context: ProceduralContext) -> None:
    """traffic_layout removes runroots selected in all supported forms."""

    RunrootScenario(procedural_context).run_remove_cases()


def test_runroot_use(procedural_context: ProceduralContext) -> None:
    """ATS discovers runroots from arguments, cwd, executables, and the environment."""

    RunrootScenario(procedural_context).run_use_cases()


def test_runroot_verify(procedural_context: ProceduralContext) -> None:
    """traffic_layout verifies a copied runroot successfully."""

    RunrootScenario(procedural_context).run_verify_cases()
