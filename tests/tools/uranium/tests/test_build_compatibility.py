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
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

SOURCE_ROOT = Path(__file__).parents[4]


@pytest.mark.parametrize("suffix", ["", "_UDS"])
@pytest.mark.parametrize(
    "old,new,expected", [(None, None, "OFF"), ("OFF", None, "OFF"), ("ON", None, "ON"), ("OFF", "ON", "ON"), ("ON", "OFF", "ON")])
def test_deprecated_cmake_options(tmp_path: Path, suffix: str, old: str | None, new: str | None, expected: str) -> None:
    """Migrate old entries once, including over cached Uranium defaults.

    :param tmp_path: Directory for the isolated CMake script.
    :param suffix: Empty for the test option, or the Unix-socket option suffix.
    :param old: Deprecated cache value, or None when absent.
    :param new: Existing Uranium cache value, or None when absent.
    :param expected: Expected effective Uranium option value.
    """

    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake is required")
    script = tmp_path / "options.cmake"
    script.write_text(
        f'include("{SOURCE_ROOT / "cmake/UraniumOptions.cmake"}")\n'
        f'if(NOT "${{ENABLE_URTEST{suffix}}}" STREQUAL "{expected}")\n'
        '  message(FATAL_ERROR "Unexpected Uranium option value")\nendif()\n'
        f'if(DEFINED ENABLE_AUTEST{suffix})\n'
        '  message(FATAL_ERROR "Deprecated option survived migration")\nendif()\n')
    command = [cmake]
    if old is not None:
        command.append(f"-DENABLE_AUTEST{suffix}:BOOL={old}")
    if new is not None:
        command.append(f"-DENABLE_URTEST{suffix}:BOOL={new}")
    result = subprocess.run([*command, "-P", str(script)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert ("is deprecated" in result.stderr) == (old is not None)


@pytest.mark.parametrize("suffix,value", [("SANDBOX", "/tmp/old sandbox"), ("OPTIONS", "-v -n 8"), ("OPTIONS", "")])
@pytest.mark.parametrize("new", [None, "", "new-value"])
def test_deprecated_cmake_strings(tmp_path: Path, suffix: str, value: str, new: str | None) -> None:
    """Preserve legacy string values and clear their obsolete cache keys.

    :param tmp_path: Directory for an isolated CMake script.
    :param suffix: Sandbox or options cache variable suffix.
    :param value: Original string, including spaces or empty.
    :param new: Uranium value to preserve, or None when not set.
    """

    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake is required")
    script = tmp_path / "strings.cmake"
    configured = f'set(URTEST_{suffix} "{new}" CACHE STRING "")\n' if new is not None else ""
    expected = new if new is not None else value
    script.write_text(
        configured + f'set(AUTEST_{suffix} "{value}" CACHE STRING "")\n'
        f'include("{SOURCE_ROOT / "cmake/UraniumOptions.cmake"}")\n'
        f'if(NOT "${{URTEST_{suffix}}}" STREQUAL "{expected}" OR DEFINED AUTEST_{suffix})\n'
        '  message(FATAL_ERROR "String migration failed")\nendif()\n')
    result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("case", ["old-on", "old-off-new-on", "old-on-new-uds"])
def test_existing_autest_build_configures(tmp_path: Path, case: str) -> None:
    """Reconfigure a real ATS tree seeded with all old AuTest cache entries.

    Set ATS_CMAKE_COMPATIBILITY_BUILD to a disposable, configured ATS build
    directory with the desired toolchain and dependencies. This test changes
    its testing options, but does not build or run ATS.

    :param tmp_path: Directory for the base-style initial cache file.
    :param case: Cache migration and explicit-option combination to verify.
    """

    directory = os.environ.get("ATS_CMAKE_COMPATIBILITY_BUILD")
    if directory is None:
        pytest.skip("set ATS_CMAKE_COMPATIBILITY_BUILD to a disposable configured ATS build")
    build = Path(directory).resolve()
    assert (build / "CMakeCache.txt").is_file()
    cmake = shutil.which("cmake")
    assert cmake is not None
    command = [cmake, "-S", str(SOURCE_ROOT), "-B", str(build)]
    seed = tmp_path / "base-cache.cmake"
    old = "OFF" if case == "old-off-new-on" else "ON"
    seed.write_text(
        f'set(ENABLE_AUTEST {old} CACHE BOOL "")\n'
        'set(ENABLE_AUTEST_UDS OFF CACHE BOOL "")\n'
        'set(AUTEST_SANDBOX "/tmp/ats-compatibility-sandbox" CACHE STRING "")\n'
        'set(AUTEST_OPTIONS "-v -n 8" CACHE STRING "")\n')
    requested = ["-DENABLE_URTEST:BOOL=OFF"] if case == "old-on" else ["-DENABLE_URTEST:BOOL=ON"]
    if case == "old-on-new-uds":
        requested.append("-DENABLE_URTEST_UDS:BOOL=ON")
    sandbox = "/tmp/ats-compatibility-sandbox"
    options = "-v -n 8"
    if case == "old-off-new-on":
        sandbox, options = "/tmp/ats-explicit-sandbox", "-q -k basic"
        requested.extend([f"-DURTEST_SANDBOX:STRING={sandbox}", f"-DURTEST_OPTIONS:STRING={options}"])
    seeded = subprocess.run(
        [*command, "-UENABLE_URTEST*", "-UURTEST_*", "-UENABLE_AUTEST*", "-UAUTEST_*", "-C",
         str(seed), *requested],
        capture_output=True,
        text=True,
        timeout=180)
    assert seeded.returncode == 0, seeded.stdout + seeded.stderr
    cache = (build / "CMakeCache.txt").read_text()
    assert "ENABLE_URTEST:BOOL=ON" in cache
    assert f"URTEST_SANDBOX:STRING={sandbox}" in cache
    assert f"URTEST_OPTIONS:STRING={options}" in cache
    uds = "ON" if case == "old-on-new-uds" else "OFF"
    assert f"ENABLE_URTEST_UDS:BOOL={uds}" in cache
    wrapper = (build / "tests/urtest.sh").read_text()
    assert ('configured_curl_uds="--curl-uds"' in wrapper) == (uds == "ON")
    assert not any(line.startswith(("ENABLE_AUTEST", "AUTEST_")) for line in cache.splitlines())
    repeated = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert repeated.returncode == 0, repeated.stdout + repeated.stderr
    assert "is deprecated" not in repeated.stderr
    targets = subprocess.run([cmake, "--build", str(build), "--target", "help"], capture_output=True, text=True, timeout=30)
    assert targets.returncode == 0, targets.stdout + targets.stderr
    for target in ("autest", "autest_no_install", "autest-uds"):
        assert re.search(rf"(?m)^\s*(?:\.\.\. )?{re.escape(target)}(?::|\s|$)", targets.stdout)


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
