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
"""Execution context and command results for procedural Uranium tests."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from ..runtime import TestRuntime


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


@dataclass(frozen=True)
class ProceduralContext:
    """Runtime and sandbox state shared by one procedural pytest item."""

    runtime: TestRuntime
    node_name: str
    test_path: Path
    run_directory: Path
    use_uds: bool = False

    @classmethod
    def create(
        cls,
        runtime: TestRuntime,
        node_name: str,
        test_path: Path,
        *,
        use_uds: bool = False,
    ) -> "ProceduralContext":
        """Create and empty the sandbox for one pytest test.

        :param runtime: Shared Uranium runtime configuration.
        :param node_name: Pytest node identifier used to name the sandbox.
        :param test_path: Path to the procedural test module.
        :param use_uds: Whether curl clients should use ATS Unix sockets.
        """

        run_directory = runtime.procedural_sandbox(node_name)
        runtime.prepare_sandbox(run_directory)
        return cls(runtime, node_name, test_path, run_directory, use_uds)

    @property
    def test_directory(self) -> Path:
        """Return the source directory containing the procedural test."""

        return self.test_path.parent

    def resolve_path(self, value: str | Path) -> Path:
        """Resolve a source or build artifact used by this test.

        :param value: Absolute path or path relative to the test directory.
        """

        path = Path(value)
        if path.is_absolute():
            return path
        source = self.test_directory / path
        return source if source.exists() else self.runtime.resolve_artifact(self.test_directory, str(value))
