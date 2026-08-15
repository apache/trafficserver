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
import os
import signal
import subprocess
import time


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
    ) -> None:
        self.name = name
        self.command = [str(argument) for argument in command]
        self.run_directory = run_directory
        self.environment = environment
        self.expected_return_codes = set(expected_return_codes)
        self.stdout_path = run_directory / f"{name}.stdout"
        self.stderr_path = run_directory / f"{name}.stderr"
        self._stdout = None
        self._stderr = None
        self._process: subprocess.Popen[bytes] | None = None

    @property
    def return_code(self) -> int | None:
        """Return the process exit status, or None while it is running."""

        return None if self._process is None else self._process.poll()

    def start(self) -> None:
        """Start the process in a new process group."""

        self.run_directory.mkdir(parents=True, exist_ok=True)
        self._stdout = self.stdout_path.open("wb")
        self._stderr = self.stderr_path.open("wb")
        self._process = subprocess.Popen(
            self.command,
            cwd=self.run_directory,
            env=self.environment,
            stdout=self._stdout,
            stderr=self._stderr,
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

        if return_code not in self.expected_return_codes:
            raise ProcessError(
                f"{self.name} exited with status {return_code}; expected "
                f"{sorted(self.expected_return_codes)}.\n{self.output()}")

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
        for stream in (self._stdout, self._stderr):
            if stream is not None and not stream.closed:
                stream.flush()

    def _close_streams(self) -> None:
        for stream in (self._stdout, self._stderr):
            if stream is not None and not stream.closed:
                stream.close()
