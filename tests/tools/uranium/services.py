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
"""Procedural service objects exposed by Uranium pytest fixtures."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
import subprocess
from typing import Any

from .scenario import FileNode, Process, UraniumTest


@dataclass(frozen=True)
class CommandResult:
    """The observable result of a client command."""

    command: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str

    @property
    def output(self) -> str:
        """Return stdout and stderr together for assertion diagnostics."""

        return self.stdout + self.stderr


class RecordsConfig:
    """Stage records.yaml updates without exposing scenario internals."""

    def __init__(self, config_file: FileNode) -> None:
        self._config_file = config_file

    def update(self, values: Mapping[str, object]) -> None:
        """Merge configuration records into the staged records.yaml file."""

        self._config_file.update(values)


class ATS:
    """A fixture-owned Traffic Server process."""

    def __init__(self, scenario: UraniumTest, name: str = "ats", **process_options: Any) -> None:
        self._process: Process = scenario.MakeATSProcess(name, **process_options)
        self._was_started = False
        self.records = RecordsConfig(self._process.Disk.records_config)

    @property
    def http_port(self) -> int:
        """Return the IPv4 HTTP listening port selected for this process."""

        return int(self._process.Variables.port)

    @property
    def uds_path(self) -> str:
        """Return the Unix-domain socket path selected for this process."""

        return str(self._process.Variables.uds_path)

    @property
    def is_running(self) -> bool:
        """Return whether Traffic Server is still running."""

        return self._process.is_running()

    def start(self) -> None:
        """Materialize configuration and wait until Traffic Server is ready."""

        self._process.start()
        self._was_started = True

    def close(self) -> None:
        """Stop Traffic Server and validate its diagnostics."""

        self._process.stop()
        if self._was_started:
            self._process.validate()


class ATSFactory:
    """Create Traffic Server instances owned by one pytest scenario."""

    def __init__(self, scenario: UraniumTest) -> None:
        self._scenario = scenario
        self._services: list[ATS] = []
        self._names: set[str] = set()

    def create(self, name: str | None = None, **process_options: Any) -> ATS:
        """Create a named Traffic Server with fixture-owned cleanup."""

        process_name = name or f"ats{len(self._services) + 1}"
        if process_name in self._names:
            raise ValueError(f"Traffic Server process {process_name!r} already exists")
        service = ATS(self._scenario, process_name, **process_options)

        self._services.append(service)
        self._names.add(process_name)
        return service

    def close(self) -> None:
        """Stop and validate every created instance in reverse order."""

        failures: list[Exception] = []
        for service in reversed(self._services):
            try:
                service.close()
            except Exception as error:
                failures.append(error)
        self._services.clear()
        self._names.clear()
        if failures:
            raise ExceptionGroup("Traffic Server cleanup failed", failures)


class Curl:
    """Run curl requests and return ordinary Python result objects."""

    def __init__(self, working_directory: Path, *, use_uds: bool = False) -> None:
        self._working_directory = working_directory
        self._use_uds = use_uds

    def get(
            self,
            ats: ATS,
            path: str = "/",
            *,
            headers: Mapping[str, str] | None = None,
            options: Sequence[str] = (),
            timeout: float = 30,
    ) -> CommandResult:
        """Issue a GET request to @a path through @a ats."""

        request_path = path if path.startswith("/") else f"/{path}"
        arguments = []
        if self._use_uds:
            arguments.extend(["--unix-socket", ats.uds_path])
        arguments.extend(options)
        for name, value in (headers or {}).items():
            arguments.extend(["--header", f"{name}: {value}"])
        arguments.append(f"http://127.0.0.1:{ats.http_port}{request_path}")
        return self.run(*arguments, timeout=timeout)

    def run(self, *arguments: str, timeout: float = 30) -> CommandResult:
        """Run curl with @a arguments without interpreting the response."""

        command = ("curl", *arguments)
        completed = subprocess.run(
            command,
            cwd=self._working_directory,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)
