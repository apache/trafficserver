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
import pwd
import subprocess

import pytest

from tools.uranium.services import ProceduralContext


def run(
        *arguments: str | Path,
        cwd: Path | None = None,
        environment: dict[str, str] | None = None,
        expected_return_codes: tuple[int, ...] = (0,),
        _directory: Path,
        _traffic_layout: Path) -> subprocess.CompletedProcess[str]:
    """Run traffic_layout and return its captured output.

    :param _directory: Test-local directory configured by the test.
    :param _traffic_layout: Test-local traffic layout configured by the test.
    :param cwd: Cwd used by this test step.
    :param environment: Environment used by this test step.
    :param expected_return_codes: Expected return codes for this case.
    :param arguments: Arguments used by this test step.
    """

    result = subprocess.run(
        [_traffic_layout, *(str(argument) for argument in arguments)],
        cwd=cwd or _directory,
        env=environment,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode in expected_return_codes, result.stdout + result.stderr
    return result


def init(path: Path, *, force: bool = False, cwd: Path | None = None, _directory: Path, _traffic_layout: Path) -> None:
    """Create one runroot and verify its metadata file.

    :param _directory: Test-local directory configured by the test.
    :param _traffic_layout: Test-local traffic layout configured by the test.
    :param path: Resource or file path used by this operation.
    :param force: Force used by this test step.
    :param cwd: Cwd used by this test step.
    """

    arguments = ["init"]
    if force:
        arguments.append("--force")
    arguments.extend(["--path", path if path.is_absolute() else path.name])
    run(*arguments, cwd=cwd, _directory=_directory, _traffic_layout=_traffic_layout)
    resolved = path if path.is_absolute() else (cwd or _directory) / path
    assert (resolved / "runroot.yaml").is_file()


def require_prefix_layout(*, _layout: dict[str, str]) -> tuple[str, str]:
    """Return prefix-relative bin and log directories or skip.

    :param _layout: Test-local layout configured by the test.
    """

    prefix = _layout["PREFIX"]
    bindir = _layout["BINDIR"]
    logdir = _layout["LOGDIR"]
    if not bindir.startswith(prefix):
        pytest.skip("traffic_layout BINDIR must be below PREFIX")
    bin_suffix = os.path.relpath(bindir, prefix)
    log_suffix = os.path.relpath(logdir, prefix) if logdir.startswith(prefix) else logdir.lstrip("/")
    return bin_suffix, log_suffix


def test_runroot_errors(procedural_context: ProceduralContext) -> None:
    """Invalid runroot operations report the expected diagnostics.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = context.run_directory
    _traffic_layout = context.runtime.ats_bin / "traffic_layout"
    _layout = context.runtime.layout

    path = _directory / "runroot"
    init(path, _directory=_directory, _traffic_layout=_traffic_layout)
    result = run("init", "--path", path, _directory=_directory, _traffic_layout=_traffic_layout)
    assert "Using existing runroot" in result.stdout + result.stderr

    nested = path / "runroot"
    result = run("init", "--path", nested, expected_return_codes=(70,), _directory=_directory, _traffic_layout=_traffic_layout)
    assert "Cannot create runroot inside another runroot" in result.stdout + result.stderr
    assert not (nested / "runroot.yaml").exists()

    invalid = _directory / "missing"
    result = run("remove", "--path", invalid, expected_return_codes=(0, 70), _directory=_directory, _traffic_layout=_traffic_layout)
    assert "Unable to read" in result.stdout + result.stderr
    result = run("verify", "--path", invalid, expected_return_codes=(0, 70), _directory=_directory, _traffic_layout=_traffic_layout)
    assert "Unable to read" in result.stdout + result.stderr


def test_runroot_init(procedural_context: ProceduralContext) -> None:
    """traffic_layout initializes runroots in all supported forms.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = context.run_directory
    _traffic_layout = context.runtime.ats_bin / "traffic_layout"
    _layout = context.runtime.layout

    bin_suffix, _ = require_prefix_layout(_layout=_layout)
    first = _directory / "runroot1"
    init(first, _directory=_directory, _traffic_layout=_traffic_layout)
    init(Path("runroot2"), cwd=_directory, _directory=_directory, _traffic_layout=_traffic_layout)

    third = _directory / "runroot3"
    third.mkdir()
    run("init", cwd=third, _directory=_directory, _traffic_layout=_traffic_layout)
    assert (third / "runroot.yaml").is_file()

    fourth = _directory / "runroot4"
    fourth.mkdir()
    (fourth / "foo").touch()
    init(fourth, force=True, _directory=_directory, _traffic_layout=_traffic_layout)

    junk = first / bin_suffix / "junk"
    junk.touch()
    fifth = _directory / "runroot5"
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


def test_runroot_remove(procedural_context: ProceduralContext) -> None:
    """traffic_layout removes runroots selected in all supported forms.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = context.run_directory
    _traffic_layout = context.runtime.ats_bin / "traffic_layout"
    _layout = context.runtime.layout

    paths = [_directory / f"runroot{number}" for number in range(1, 4)]
    for path in paths:
        init(path, _directory=_directory, _traffic_layout=_traffic_layout)
    run("remove", "--path", paths[0], _directory=_directory, _traffic_layout=_traffic_layout)
    assert not paths[0].exists()
    run("remove", "--path", paths[1].name, cwd=_directory, _directory=_directory, _traffic_layout=_traffic_layout)
    assert not paths[1].exists()
    run("remove", cwd=paths[2], _directory=_directory, _traffic_layout=_traffic_layout)
    assert paths[2].is_dir()
    assert not (paths[2] / "runroot.yaml").exists()


def test_runroot_use(procedural_context: ProceduralContext) -> None:
    """ATS discovers runroots from arguments, cwd, executables, and the environment.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = context.run_directory
    _traffic_layout = context.runtime.ats_bin / "traffic_layout"
    _layout = context.runtime.layout

    bin_suffix, _ = require_prefix_layout(_layout=_layout)
    first = _directory / "runroot1"
    second = _directory / "runroot2"
    init(first, _directory=_directory, _traffic_layout=_traffic_layout)
    init(second, _directory=_directory, _traffic_layout=_traffic_layout)
    assert f"PREFIX: {first}" in run("info", f"--run-root={first}", _directory=_directory, _traffic_layout=_traffic_layout).stdout
    assert f"PREFIX: {first}" in run("info", cwd=first, _directory=_directory, _traffic_layout=_traffic_layout).stdout

    copied_layout = first / bin_suffix / "traffic_layout"
    result = subprocess.run([copied_layout, "info"], capture_output=True, text=True, timeout=60, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"PREFIX: {first}" in result.stdout

    environment = os.environ.copy()
    environment["TS_RUNROOT"] = str(second)
    assert f"PREFIX: {second}" in run(
        "info", environment=environment, _directory=_directory, _traffic_layout=_traffic_layout).stdout


def test_runroot_verify(procedural_context: ProceduralContext) -> None:
    """traffic_layout verifies a copied runroot successfully.

    :param procedural_context: Procedural context used by this test step.
    """
    context = procedural_context
    _directory = context.run_directory
    _traffic_layout = context.runtime.ats_bin / "traffic_layout"
    _layout = context.runtime.layout

    bin_suffix, log_suffix = require_prefix_layout(_layout=_layout)
    path = _directory / "runroot"
    init(path, _directory=_directory, _traffic_layout=_traffic_layout)
    runroot_yaml = path / "runroot.yaml"
    runroot_yaml.write_text(
        runroot_yaml.read_text().replace(
            f"runtimedir: {_layout['RUNTIMEDIR']}",
            "runtimedir: ./var/trafficserver",
        ))
    # Initialization copies files as the invoking user, including any
    # existing installed logs. Verify that user's permissions unchanged.
    username = pwd.getpwuid(os.getuid()).pw_name
    first = run("verify", "--path", path, "--with-user", username, _directory=_directory, _traffic_layout=_traffic_layout).stdout
    for expected in (str(path / bin_suffix), str(path / log_suffix), "PASSED"):
        assert expected in first

    copied_layout = path / bin_suffix / "traffic_layout"
    result = subprocess.run(
        [copied_layout, "verify", "--path", path, "--with-user", username],
        cwd=path,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    for expected in (str(path / bin_suffix), str(path / log_suffix), "PASSED"):
        assert expected in result.stdout

    inaccessible = path / log_suffix / "unwritable.log"
    inaccessible.touch(mode=0o400)
    failed = run(
        "verify",
        "--path",
        path,
        "--with-user",
        username,
        expected_return_codes=(70,),
        _directory=_directory,
        _traffic_layout=_traffic_layout)
    assert "Write permission failed" in failed.stdout
    assert str(inaccessible) in failed.stdout
