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
"""Collect ``*.test.yaml`` Proxy Verifier files as pytest items."""

from __future__ import annotations

from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any
import os

import pytest

from .config import ReplayConfigError, ReplaySpec
from .process import ProcessError
from .replay import ReplaySkip, ReplayTest
from .runtime import TestRuntime


def pytest_addoption(parser: pytest.Parser) -> None:
    """Register ATS end-to-end runtime paths."""

    group = parser.getgroup("ATS replay tests")
    group.addoption("--ats-bin", help="Directory containing installed ATS executables")
    group.addoption("--proxy-verifier-bin", help="Directory containing verifier-client and verifier-server")
    group.addoption("--build-root", help="ATS build directory containing test plugins")
    group.addoption("--sandbox", help="Directory for isolated test process trees")


def pytest_configure(config: pytest.Config) -> None:
    """Register the replay marker used for selection."""

    config.addinivalue_line("markers", "ats_replay: ATS system test driven by a Proxy Verifier replay file")


def pytest_ignore_collect(collection_path: Path, config: pytest.Config) -> bool | None:
    """Leave legacy ``*.test.py`` files exclusively to AuTest."""

    del config
    return True if collection_path.name.endswith(".test.py") else None


def pytest_collect_file(file_path: Path, parent: pytest.Collector) -> pytest.File | None:
    """Collect replay files whose basename ends in ``.test.yaml`` or ``.test.yml``."""

    if file_path.name.endswith((".test.yaml", ".test.yml")):
        return ReplayFile.from_parent(parent, path=file_path)
    return None


class ReplayFile(pytest.File):
    """Represent one directly collected replay file."""

    def collect(self) -> Iterator[pytest.Item]:
        """Load collection metadata and yield one test item."""

        try:
            spec = ReplaySpec.load(Path(self.path))
        except ReplayConfigError as error:
            raise self.CollectError(str(error)) from error
        suffix = ".test.yaml" if spec.path.name.endswith(".test.yaml") else ".test.yml"
        item = ReplayItem.from_parent(self, name=spec.path.name.removesuffix(suffix), spec=spec)
        item.add_marker("ats_replay")
        yield item


class ReplayItem(pytest.Item):
    """Execute one replay file as a pytest test item."""

    def __init__(self, *, spec: ReplaySpec, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        self.spec = spec

    def runtest(self) -> None:
        """Run this item's replay lifecycle."""

        runtime = get_runtime(self.config)
        try:
            ReplayTest(self.spec, runtime, self.nodeid).run()
        except ReplaySkip as reason:
            pytest.skip(str(reason))

    def repr_failure(self, excinfo: pytest.ExceptionInfo[BaseException], style: str | None = None) -> str:
        """Present system-test failures without pytest's internal collector frames."""

        if isinstance(excinfo.value, (AssertionError, ProcessError, ReplayConfigError)):
            return str(excinfo.value)
        return super().repr_failure(excinfo, style=style)

    def reportinfo(self) -> tuple[Path, int, str]:
        """Identify the replay file in pytest reports."""

        return self.spec.path, 0, self.spec.description


def get_runtime(config: pytest.Config) -> TestRuntime:
    """Build and cache the runtime selected by pytest command-line options."""

    cache_name = "_ats_test_runtime"
    cached = getattr(config, cache_name, None)
    if cached is not None:
        return cached

    repository_root = Path(__file__).resolve().parents[2]
    values = {
        "ats_bin": config.getoption("ats_bin") or os.environ.get("ATS_BIN"),
        "verifier_bin": config.getoption("proxy_verifier_bin") or os.environ.get("PROXY_VERIFIER_BIN"),
        "build_root": config.getoption("build_root") or os.environ.get("ATS_BUILD_ROOT"),
        "sandbox": config.getoption("sandbox") or os.environ.get("ATS_TEST_SANDBOX"),
    }
    missing = [name for name, value in values.items() if not value]
    if missing:
        raise pytest.UsageError("Replay tests require " + ", ".join(f"--{name.replace('_', '-')}" for name in missing))
    worker = os.environ.get("PYTEST_XDIST_WORKER", "main")
    runtime = TestRuntime.create(
        repository_root=repository_root,
        build_root=Path(values["build_root"]),
        ats_bin=Path(values["ats_bin"]),
        verifier_bin=Path(values["verifier_bin"]),
        sandbox_root=Path(values["sandbox"]) / worker,
    )
    setattr(config, cache_name, runtime)
    return runtime


@pytest.fixture(scope="session")
def ats_test_runtime(pytestconfig: pytest.Config) -> TestRuntime:
    """Provide installed ATS paths and capabilities to handwritten tests."""

    return get_runtime(pytestconfig)


@pytest.fixture
def ats_replay(ats_test_runtime: TestRuntime, request: pytest.FixtureRequest) -> Callable[[Path], None]:
    """Return a helper that executes a replay file from a handwritten pytest test."""

    def run(path: Path) -> None:
        spec = ReplaySpec.load(path)
        ReplayTest(spec, ats_test_runtime, request.node.nodeid).run()

    return run
