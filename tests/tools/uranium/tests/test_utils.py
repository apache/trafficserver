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

from tools.uranium.utils import python_environment_executable


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
