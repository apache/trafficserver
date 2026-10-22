# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements. See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Preserve release-branch build compatibility across the test migration."""

import json
from pathlib import Path
import shutil
import subprocess

import pytest

SOURCE_ROOT = Path(__file__).parents[4]


@pytest.mark.parametrize("suffix", ["", "_UDS"])
@pytest.mark.parametrize(
    "old,new,expected", [(None, None, "OFF"), ("OFF", None, "OFF"), ("ON", None, "ON"), ("OFF", "ON", "ON"), ("ON", "OFF", "OFF")])
def test_deprecated_cmake_options(tmp_path: Path, suffix: str, old: str | None, new: str | None, expected: str) -> None:
    """Accept old cache entries without overriding an explicit new option.

    :param tmp_path: Directory for the isolated CMake script.
    :param suffix: Empty for the test option, or the Unix-socket option suffix.
    :param old: Deprecated cache value, or None when absent.
    :param new: Explicit Uranium cache value, or None when absent.
    :param expected: Expected effective Uranium option value.
    """

    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake is required")
    script = tmp_path / "options.cmake"
    script.write_text(
        f'include("{SOURCE_ROOT / "cmake/UraniumOptions.cmake"}")\n'
        f'if(NOT "${{ENABLE_URTEST{suffix}}}" STREQUAL "{expected}")\n'
        '  message(FATAL_ERROR "Unexpected Uranium option value")\nendif()\n')
    command = [cmake]
    if old is not None:
        command.append(f"-DENABLE_AUTEST{suffix}:BOOL={old}")
    if new is not None:
        command.append(f"-DENABLE_URTEST{suffix}:BOOL={new}")
    result = subprocess.run([*command, "-P", str(script)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert ("is deprecated" in result.stderr) == (old is not None)


def test_deprecated_cmake_presets() -> None:
    """Retain all previously supported AuTest preset spellings and paths."""

    presets = json.loads((SOURCE_ROOT / "CMakePresets.json").read_text())
    configure = {preset["name"]: preset for preset in presets["configurePresets"]}
    for old in ("autest", "autest-uds", "branch-autest", "branch-autest-uds", "ci-fedora-autest"):
        assert old in configure
    for old in ("autest", "autest-uds"):
        assert configure[old]["binaryDir"] == "${sourceDir}/build-autest"
        assert configure[old]["cacheVariables"]["CMAKE_INSTALL_PREFIX"] == "/tmp/ts-autest"
    build = {preset["name"]: preset for preset in presets["buildPresets"]}
    assert build["autest"]["configurePreset"] == "autest"
    assert build["autest"]["targets"] == ["urtest"]
