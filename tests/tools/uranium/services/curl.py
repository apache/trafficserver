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
"""Curl client service for procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path
import shlex
import subprocess
from typing import TYPE_CHECKING

from .context import CommandResult

if TYPE_CHECKING:
    from .ats import ATS


class Curl:
    """Run curl requests and return ordinary Python result objects."""

    def __init__(self, working_directory: Path, *, use_uds: bool = False) -> None:
        """Create a curl command runner.

        :param working_directory: Directory in which curl commands execute.
        :param use_uds: Whether requests targeting ATS should use its Unix
            domain socket instead of its TCP listener.
        """

        self._working_directory = working_directory
        self._use_uds = use_uds

    @property
    def uses_uds(self) -> bool:
        return self._use_uds

    @staticmethod
    def supports(feature: str) -> bool:
        """Return whether the installed curl advertises a feature.

        :param feature: Case-insensitive feature name to find in
            ``curl --version`` output.
        """

        result = subprocess.run(("curl", "--version"), capture_output=True, text=True, check=False)
        return feature.lower() in (result.stdout + result.stderr).lower()

    def get(
        self,
        ats: ATS,
        path: str = "/",
        *,
        headers: Mapping[str, str] | None = None,
        options: str = "",
        timeout: float = 30,
    ) -> CommandResult:
        """Send a GET request to an ATS instance.

        :param ats: ATS instance that receives the request.
        :param path: Request path, with or without a leading slash.
        :param headers: HTTP headers to add to the request.
        :param options: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        request_path = path if path.startswith("/") else f"/{path}"
        arguments = []
        if self._use_uds:
            arguments.extend(["--unix-socket", ats.uds_path])
        arguments.extend(shlex.split(options))
        for name, value in (headers or {}).items():
            arguments.extend(["--header", f"{name}: {value}"])
        arguments.append(f"http://127.0.0.1:{ats.http_port}{request_path}")
        return self._run(arguments, timeout=timeout)

    def run_for(self, ats: ATS, arguments: str, timeout: float = 30) -> CommandResult:
        """Run curl arguments using the selected ATS transport.

        :param ats: ATS instance whose Unix socket is used when UDS mode is
            enabled.
        :param arguments: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        command_arguments = []
        if self._use_uds:
            command_arguments.extend(("--unix-socket", ats.uds_path))
        command_arguments.extend(shlex.split(arguments))
        return self._run(command_arguments, timeout=timeout)

    def run_script(self, ats: ATS, script: str, timeout: float = 30) -> CommandResult:
        """Run a Bash script containing curl command placeholders.

        :param ats: ATS instance whose Unix socket is substituted for
            ``{curl}`` when UDS mode is enabled.
        :param script: Bash script in which ``{curl}`` is the transport-aware
            curl command and ``{curl_base}`` is plain curl.
        :param timeout: Maximum number of seconds to wait for the script.
        """

        curl_command = "curl"
        if self._use_uds:
            curl_command += f" --unix-socket {shlex.quote(ats.uds_path)}"
        rendered = script.replace("{curl}", curl_command).replace("{curl_base}", "curl")
        command = ("/bin/bash", "-o", "pipefail", "-c", rendered)
        completed = subprocess.run(
            command,
            cwd=self._working_directory,
            capture_output=True,
            text=True,
            timeout=timeout,
            shell=False,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)

    def run(self, arguments: str, timeout: float = 30) -> CommandResult:
        """Run curl without applying an ATS transport.

        :param arguments: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        return self._run(shlex.split(arguments), timeout=timeout)

    def _run(self, arguments: Sequence[str], *, timeout: float) -> CommandResult:
        """Execute an already tokenized curl command.

        :param arguments: Curl argument vector without the executable name.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        command = ("curl", *arguments)
        completed = subprocess.run(
            command,
            cwd=self._working_directory,
            capture_output=True,
            text=True,
            timeout=timeout,
            shell=False,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)
