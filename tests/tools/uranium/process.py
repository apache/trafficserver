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
"""Process lifecycle helpers for ATS Uranium tests."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Sequence
from pathlib import Path
from typing import Any
import os
import signal
import subprocess
import time

from .expectations import StreamExpectations


class ProcessError(RuntimeError):
    """Report an unexpected process exit or readiness failure."""


class ManagedProcess:
    """Own a subprocess and its captured output files."""

    def __init__(
            self,
            name: str,
            command: Sequence[str],
            run_directory: Path,
            environment: dict[str, str] | None = None,
            expected_return_codes: Iterable[int] = (0,),
            test_directory: Path | None = None,
    ) -> None:
        """Create a managed process and its explicit expectations.

        :param name: Process name used in diagnostics and captured filenames.
        :param command: Executable followed by its command-line arguments.
        :param run_directory: Working directory for the process.
        :param environment: Complete process environment, or ``None`` to
            inherit the current environment.
        :param expected_return_codes: Initial process exit statuses treated as
            success.
        :param test_directory: Source directory for relative gold-file paths,
            or ``None`` to use the run directory.
        """

        self.name = name
        self.command = [str(argument) for argument in command]
        self.run_directory = run_directory
        self.environment = environment
        self.stdout_path = run_directory / f"{name}.stdout"
        self.stderr_path = run_directory / f"{name}.stderr"
        expectation_directory = test_directory or run_directory
        self._stdout = StreamExpectations(name, "stdout", self.stdout_path, expectation_directory)
        self._stderr = StreamExpectations(name, "stderr", self.stderr_path, expectation_directory)
        self._return_codes: tuple[int, ...] = (0,)
        self.expect_return_codes(*tuple(expected_return_codes))
        self._stdout_file = None
        self._stderr_file = None
        self._process: subprocess.Popen[bytes] | None = None

    @property
    def stdout(self) -> StreamExpectations:
        """Return the read-only standard-output expectation API."""

        return self._stdout

    @stdout.setter
    def stdout(self, _value: Any) -> None:
        """Reject replacement of the stdout expectation object.

        :param _value: Value supplied by an unsupported assignment.
        """

        raise AttributeError("stdout is read-only; use stdout.contains(), stdout.excludes(), or stdout.matches_gold()")

    @property
    def stderr(self) -> StreamExpectations:
        """Return the read-only standard-error expectation API."""

        return self._stderr

    @stderr.setter
    def stderr(self, _value: Any) -> None:
        """Reject replacement of the stderr expectation object.

        :param _value: Value supplied by an unsupported assignment.
        """

        raise AttributeError("stderr is read-only; use stderr.contains(), stderr.excludes(), or stderr.matches_gold()")

    @property
    def return_codes(self) -> tuple[int, ...]:
        """Return the acceptable process exit statuses."""

        return self._return_codes

    @return_codes.setter
    def return_codes(self, _values: Any) -> None:
        """Reject direct assignment of acceptable exit statuses.

        :param _values: Value supplied by an unsupported assignment.
        """

        raise AttributeError("return_codes is read-only; use expect_return_codes()")

    def expect_return_codes(self, *codes: int) -> None:
        """Set the acceptable process exit statuses.

        :param codes: One or more integer exit statuses treated as success.
        """

        if not codes:
            raise ValueError("expect_return_codes() requires at least one status")
        if not all(isinstance(code, int) for code in codes):
            raise TypeError("expect_return_codes() statuses must be integers")
        self._return_codes = codes

    @property
    def stdout_text(self) -> str:
        """Return captured standard output."""

        self._flush_streams()
        return self.stdout_path.read_text(errors="replace") if self.stdout_path.exists() else ""

    @property
    def stderr_text(self) -> str:
        """Return captured standard error."""

        self._flush_streams()
        return self.stderr_path.read_text(errors="replace") if self.stderr_path.exists() else ""

    @property
    def return_code(self) -> int | None:
        """Return the process exit status, or None while it is running."""

        return None if self._process is None else self._process.poll()

    def start(self) -> None:
        """Start the process in a new process group."""

        self.run_directory.mkdir(parents=True, exist_ok=True)
        self._stdout_file = self.stdout_path.open("wb")
        self._stderr_file = self.stderr_path.open("wb")
        self._process = subprocess.Popen(
            self.command,
            cwd=self.run_directory,
            env=self.environment,
            stdout=self._stdout_file,
            stderr=self._stderr_file,
            start_new_session=True,
        )

    def wait_until(self, ready: Callable[[], bool], timeout: float, description: str) -> None:
        """Wait until @a ready succeeds while ensuring the process remains alive."""

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            return_code = self.return_code
            if return_code is not None:
                self._close_streams()
                raise ProcessError(
                    f"{self.name} exited with status {return_code} while waiting for {description}.\n{self.output()}")
            if ready():
                return
            time.sleep(0.05)

        raise ProcessError(f"Timed out after {timeout:g}s waiting for {self.name}: {description}.\n{self.output()}")

    def wait(self, timeout: float) -> None:
        """Wait for a one-shot process and validate its return code."""

        if self._process is None:
            raise ProcessError(f"{self.name} has not been started")
        try:
            return_code = self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            self.stop()
            raise ProcessError(f"{self.name} timed out after {timeout:g}s.\n{self.output()}") from error
        finally:
            self._close_streams()

        if return_code not in self.return_codes:
            raise ProcessError(
                f"{self.name} exited with status {return_code}; expected "
                f"{sorted(self.return_codes)}.\n{self.output()}")

    def validate_output(self) -> None:
        """Apply registered stdout and stderr expectations."""

        self.stdout.validate(self.stdout_text)
        self.stderr.validate(self.stderr_text)

    def send_signal(self, signal_number: int) -> None:
        """Send @a signal_number to the process group leader."""

        if self._process is None or self._process.poll() is not None:
            raise ProcessError(f"{self.name} is not running")
        self._process.send_signal(signal_number)

    def stop(self, timeout: float = 5.0) -> None:
        """Stop the process group, escalating to SIGKILL if necessary."""

        if self._process is None:
            self._close_streams()
            return

        return_code = self._process.poll()
        if return_code is None:
            try:
                os.killpg(self._process.pid, signal.SIGTERM)
                self._process.wait(timeout=timeout)
            except ProcessLookupError:
                pass
            except subprocess.TimeoutExpired:
                os.killpg(self._process.pid, signal.SIGKILL)
                self._process.wait(timeout=timeout)
        self._close_streams()

    def output(self) -> str:
        """Return the captured stdout and stderr for diagnostics."""

        self._flush_streams()
        sections = []
        for label, path in (("stdout", self.stdout_path), ("stderr", self.stderr_path)):
            if path.exists():
                sections.append(f"--- {self.name} {label} ---\n{path.read_text(errors='replace')}")
        return "\n".join(sections)

    def _flush_streams(self) -> None:
        for stream in (self._stdout_file, self._stderr_file):
            if stream is not None and not stream.closed:
                stream.flush()

    def _close_streams(self) -> None:
        for stream in (self._stdout_file, self._stderr_file):
            if stream is not None and not stream.closed:
                stream.close()
