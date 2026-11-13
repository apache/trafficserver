#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Unit tests for shared Uranium utilities."""

from pathlib import Path

import pytest

from tools.uranium.utils import go_mod_version, go_toolchain_skip_reason, python_environment_executable, version_tuple


def test_console_script_resolves_beside_the_interpreter(monkeypatch: pytest.MonkeyPatch) -> None:
    """Find Python console scripts without depending on the launcher's PATH.

    :param monkeypatch: Pytest function and interpreter patch helper.
    """

    calls: list[tuple[str, str | None]] = []

    def resolve(name: str, *, path: str | None = None) -> str | None:
        """Record the explicit search path and return a synthetic executable.

        :param name: Executable name being resolved.
        :param path: Explicit executable search path.
        :return: Synthetic resolved executable path.
        """

        calls.append((name, path))
        return f"{path}/{name}"

    monkeypatch.setattr("tools.uranium.utils.sys.executable", "/venv/bin/python")
    monkeypatch.setattr("tools.uranium.utils.shutil.which", resolve)

    assert python_environment_executable("microdns") == "/venv/bin/microdns"
    assert calls == [("microdns", str(Path("/venv/bin")))]


def test_console_script_falls_back_to_path_lookup(monkeypatch: pytest.MonkeyPatch) -> None:
    """Preserve ordinary PATH lookup when the script is not beside Python.

    :param monkeypatch: Pytest function patch helper.
    """

    monkeypatch.setattr("tools.uranium.utils.shutil.which", lambda _name, *, path=None: None)

    assert python_environment_executable("microserver") == "microserver"


@pytest.mark.parametrize(
    ("directive", "expected"),
    (("go 1.24", (1, 24)), ("go 1.25.0", (1, 25, 0)), ("go\t1.26.1 // minimum", (1, 26, 1))),
    ids=("two-part", "three-part", "tab-and-comment"),
)
def test_go_mod_version_reads_the_go_directive(tmp_path: Path, directive: str, expected: tuple[int, ...]) -> None:
    """Read the minimum Go version a module declares, ignoring other lines.

    :param tmp_path: Pytest temporary directory for the synthetic go.mod.
    :param directive: The ``go`` line written into the file.
    :param expected: The version tuple the line declares.
    """

    go_mod = tmp_path / "go.mod"
    go_mod.write_text(f"module example.test/client\n\n{directive}\n\ntoolchain go1.30.0\n\nrequire example.test/dep v1.2.3\n")
    assert go_mod_version(go_mod) == expected


def test_go_mod_version_compares_against_go_version_output(tmp_path: Path) -> None:
    """Compare ``go version`` output with the go directive: 1.24.1 is too old for 1.25.0, 1.25.0 is enough.

    :param tmp_path: Pytest temporary directory for the synthetic go.mod.
    """

    go_mod = tmp_path / "go.mod"
    go_mod.write_text("module example.test/client\n\ngo 1.25.0\n")
    assert version_tuple("go version go1.24.1 linux/amd64") < go_mod_version(go_mod)
    assert version_tuple("go version go1.25.0 linux/amd64") >= go_mod_version(go_mod)


def test_go_mod_version_rejects_a_missing_go_directive(tmp_path: Path) -> None:
    """A go.mod without a ``go`` directive is an error, not "any version".

    :param tmp_path: Pytest temporary directory for the synthetic go.mod.
    """

    go_mod = tmp_path / "go.mod"
    go_mod.write_text("module example.test/client\n\ntoolchain go1.26.1\n")
    with pytest.raises(ValueError, match="no go directive"):
        go_mod_version(go_mod)


def _write_go_mod(directory: Path, version: str) -> Path:
    """Write a minimal go.mod into a module directory.

    :param directory: Parent directory for the synthetic module.
    :param version: Version for the ``go`` directive.
    :return: Path to the written go.mod.
    """

    module = directory / "client"
    module.mkdir()
    go_mod = module / "go.mod"
    go_mod.write_text(f"module example.test/client\n\ngo {version}\n")
    return go_mod


@pytest.mark.parametrize(
    ("go_version_output", "expected"),
    (
        (None, "Go 1.25.0 or newer is required by client/go.mod; go is not on PATH"),
        ("go version go1.24.1 linux/amd64\n", "Go 1.25.0 or newer is required by client/go.mod; found Go 1.24.1"),
        ("go version go1.25.0 linux/amd64\n", None),
        ("go version go1.26.2 linux/amd64\n", None),
    ),
    ids=("go-missing", "go-too-old", "go-exact", "go-newer"),
)
def test_go_toolchain_skip_reason(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, go_version_output: str | None, expected: str | None
) -> None:
    """Skip only when go is missing or older than the go directive, probing with GOTOOLCHAIN=local.

    :param monkeypatch: Pytest function and interpreter patch helper.
    :param tmp_path: Pytest temporary directory for the synthetic module.
    :param go_version_output: Simulated ``go version`` output, or None when go is not on PATH.
    :param expected: The skip reason the helper should return.
    """

    environments: list[dict[str, str]] = []

    def check_output(command: tuple[str, ...], *, text: bool, env: dict[str, str]) -> str:
        """Record the probe environment and return the simulated version line.

        :param command: Command the helper runs.
        :param text: Whether text output was requested.
        :param env: Environment the probe runs with.
        :return: Simulated ``go version`` output.
        """

        assert command == ("go", "version") and text
        environments.append(env)
        assert go_version_output is not None
        return go_version_output

    monkeypatch.setenv("GOTOOLCHAIN", "go1.30.0")
    monkeypatch.setattr("tools.uranium.utils.shutil.which", lambda _name: None if go_version_output is None else "/usr/bin/go")
    monkeypatch.setattr("tools.uranium.utils.subprocess.check_output", check_output)

    assert go_toolchain_skip_reason(_write_go_mod(tmp_path, "1.25.0")) == expected
    assert [env["GOTOOLCHAIN"] for env in environments] == ([] if go_version_output is None else ["local"])


def test_go_toolchain_skip_reason_rejects_unreadable_go_version(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Output without a version number is an error, not a reason to skip.

    :param monkeypatch: Pytest function and interpreter patch helper.
    :param tmp_path: Pytest temporary directory for the synthetic module.
    """

    monkeypatch.setattr("tools.uranium.utils.shutil.which", lambda _name: "/usr/bin/go")
    monkeypatch.setattr("tools.uranium.utils.subprocess.check_output", lambda *_args, **_kwargs: "go version devel\n")
    with pytest.raises(ValueError, match="cannot read a Go version"):
        go_toolchain_skip_reason(_write_go_mod(tmp_path, "1.25.0"))
