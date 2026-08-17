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
"""Managed support-process service for procedural Uranium tests."""

from __future__ import annotations

from pathlib import Path
import re

from ..process import ManagedProcess
from ._service_helpers import tcp_open
from .context import CommandResult


class ProcessService:
    """A pytest-owned non-ATS process used by a procedural test."""

    def __init__(
        self,
        process: ManagedProcess,
        *,
        reject_expression: str = "",
        ready_port: int = 0,
        ready_address: str = "127.0.0.1",
    ) -> None:
        """Wrap a managed process with support-service behavior.

        :param process: Managed process owned by this service.
        :param reject_expression: Regular expression forbidden in its output.
        :param ready_port: TCP port that must accept connections after start,
            or zero to skip listener readiness checks.
        :param ready_address: Address used for listener readiness checks.
        """

        self._process = process
        self._reject_expression = reject_expression
        self._ready_port = ready_port
        self._ready_address = ready_address
        self._was_started = False
        self._was_validated = False

    @property
    def name(self) -> str:
        return self._process.name

    @property
    def run_directory(self) -> Path:
        return self._process.run_directory

    @property
    def is_running(self) -> bool:
        return self._process.return_code is None if self._was_started else False

    @property
    def stdout(self) -> str:
        self._process._flush_streams()
        return self._process.stdout_path.read_text(errors="replace") if self._process.stdout_path.exists() else ""

    @property
    def stderr(self) -> str:
        self._process._flush_streams()
        return self._process.stderr_path.read_text(errors="replace") if self._process.stderr_path.exists() else ""

    @property
    def output(self) -> str:
        return self.stdout + self.stderr

    def start(self) -> None:
        self._process.start()
        self._was_started = True
        if self._ready_port:
            self._process.wait_until(
                lambda: tcp_open(self._ready_port, self._ready_address),
                10,
                f"{self.name} listener on {self._ready_address}:{self._ready_port}",
            )

    def wait(self, timeout: float = 60) -> CommandResult:
        """Wait for the process and validate its captured output.

        :param timeout: Maximum number of seconds to wait for completion.
        """

        self._process.wait(timeout)
        self._validate_output()
        self._was_validated = True
        return CommandResult(tuple(self._process.command), int(self._process.return_code or 0), self.stdout, self.stderr)

    def run(self, timeout: float = 60) -> CommandResult:
        """Start the process and wait for completion.

        :param timeout: Maximum number of seconds to wait after startup.
        """

        self.start()
        return self.wait(timeout)

    def stop(self) -> None:
        self._process.stop()

    def close(self) -> None:
        self.stop()
        if self._was_started and not self._was_validated:
            self._validate_output()

    def _validate_output(self) -> None:
        if self._reject_expression and re.search(self._reject_expression, self.output):
            raise AssertionError(f"Unexpected diagnostic in {self.name}:\n{self.output}")
