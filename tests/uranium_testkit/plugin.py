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
import fnmatch
import os

import pytest

from .config import ReplayConfigError, ReplaySpec
from .process import ProcessError
from .replay import ReplaySkip, ReplayTest
from .runtime import TestRuntime
from .scenario import UraniumTest
from .services import ATS, Curl


def pytest_addoption(parser: pytest.Parser) -> None:
    """Register ATS Uranium test runtime paths."""

    group = parser.getgroup("ATS Uranium tests")
    group.addoption("--ats-bin", help="Directory containing installed ATS executables")
    group.addoption("--proxy-verifier-bin", help="Directory containing verifier-client and verifier-server")
    group.addoption("--build-root", help="ATS build directory containing test plugins")
    group.addoption("--sandbox", help="Directory for isolated test process trees")
    group.addoption("--urtest-filter", action="append", default=[], help="Glob selecting Uranium test names")
    group.addoption("--urtest-shard-index", type=int, help="Zero-based CI shard to collect")
    group.addoption("--urtest-shard-count", type=int, help="Total number of CI shards")
    group.addoption("--curl-uds", action="store_true", help="Run supported Uranium tests with curl Unix sockets")


def pytest_configure(config: pytest.Config) -> None:
    """Register the replay marker used for selection."""

    config.addinivalue_line("markers", "uranium_replay: Uranium test driven by a Proxy Verifier replay file")
    config.addinivalue_line("markers", "uranium_procedural: Uranium test written as a pytest function")
    config.addinivalue_line("markers", "serial: Uranium test that must run without parallel peers")
    if getattr(config.option, "numprocesses", None) and getattr(config.option, "dist", None) == "load":
        config.option.dist = "loadgroup"


def pytest_collect_file(file_path: Path, parent: pytest.Collector) -> pytest.File | None:
    """Collect direct replay files; pytest collects procedural Python normally."""

    if file_path.name.endswith((".test.yaml", ".test.yml")):
        return ReplayFile.from_parent(parent, path=file_path)
    return None


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """Apply stable name filtering and zero-based CI sharding."""

    for item in items:
        if item.path.name.startswith("test_") and "uranium_tests" in item.path.parts:
            item.add_marker("uranium_procedural")
        if _is_serial_test(Path(item.path)):
            item.add_marker("serial")
            item.add_marker(pytest.mark.xdist_group("ats_serial"))

    selected = items
    deselected: list[pytest.Item] = []
    patterns = config.getoption("urtest_filter")
    if patterns:
        selected, rejected = _partition_items(selected, lambda item: _matches_filter(item, patterns))
        deselected.extend(rejected)

    if config.getoption("curl_uds"):
        unsupported = {"h2", "tls", "tls_hooks"}
        selected, rejected = _partition_items(
            selected,
            lambda item: not _is_below_uranium_directory(item, unsupported),
        )
        deselected.extend(rejected)

    shard_index = config.getoption("urtest_shard_index")
    shard_count = config.getoption("urtest_shard_count")
    if (shard_index is None) != (shard_count is None):
        raise pytest.UsageError("--urtest-shard-index and --urtest-shard-count must be provided together")
    if shard_count is not None:
        if shard_count <= 0 or shard_index < 0 or shard_index >= shard_count:
            raise pytest.UsageError("ATS shard values must satisfy 0 <= index < count")
        shard_nodeids = {
            item.nodeid
            for position, item in enumerate(sorted(selected, key=lambda candidate: candidate.nodeid))
            if position % shard_count == shard_index
        }
        selected, rejected = _partition_items(selected, lambda item: item.nodeid in shard_nodeids)
        deselected.extend(rejected)

    if deselected:
        config.hook.pytest_deselected(items=deselected)
    regular_items = [item for item in selected if item.get_closest_marker("serial") is None]
    serial_items = sorted(
        (item for item in selected if item.get_closest_marker("serial") is not None),
        key=lambda item: ("tls_conn_timeout" not in item.name, item.nodeid),
    )
    items[:] = regular_items + serial_items


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
        item.add_marker("uranium_replay")
        yield item


class ReplayItem(pytest.Item):
    """Execute one replay file as a pytest test item."""

    def __init__(self, *, spec: ReplaySpec, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        self.spec = spec

    def runtest(self) -> None:
        """Run this item's replay lifecycle."""

        runtime = get_runtime(self.config)
        with runtime.execution_lock(is_exclusive=False):
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

    cache_name = "_uranium_test_runtime"
    cached = getattr(config, cache_name, None)
    if cached is not None:
        return cached

    repository_root = Path(__file__).resolve().parents[2]
    values = {
        "ats_bin": config.getoption("ats_bin") or os.environ.get("ATS_BIN"),
        "verifier_bin": config.getoption("proxy_verifier_bin") or os.environ.get("PROXY_VERIFIER_BIN"),
        "build_root": config.getoption("build_root") or os.environ.get("ATS_BUILD_ROOT"),
        "sandbox": config.getoption("sandbox") or os.environ.get("ATS_URTEST_SANDBOX"),
    }
    missing = [name for name, value in values.items() if not value]
    if missing:
        raise pytest.UsageError("ATS Uranium tests require " + ", ".join(f"--{name.replace('_', '-')}" for name in missing))
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
def uranium_test_runtime(pytestconfig: pytest.Config) -> TestRuntime:
    """Provide installed ATS paths and capabilities to handwritten tests."""

    return get_runtime(pytestconfig)


@pytest.fixture
def uranium_replay(uranium_test_runtime: TestRuntime, request: pytest.FixtureRequest) -> Callable[[Path], None]:
    """Return a helper that executes a replay file from a handwritten pytest test."""

    def run(path: Path) -> None:
        spec = ReplaySpec.load(path)
        ReplayTest(spec, uranium_test_runtime, request.node.nodeid).run()

    return run


@pytest.fixture
def uranium_scenario(uranium_test_runtime: TestRuntime, request: pytest.FixtureRequest) -> Iterator[UraniumTest]:
    """Own the sandbox and processes shared by procedural service fixtures."""

    is_serial = request.node.get_closest_marker("serial") is not None
    with uranium_test_runtime.execution_lock(is_exclusive=is_serial):
        scenario = UraniumTest(
            uranium_test_runtime,
            request.node.nodeid,
            Path(request.node.path),
            curl_uds=request.config.getoption("curl_uds"),
        )
        try:
            yield scenario
        finally:
            if not scenario._executed:
                scenario.cleanup()


@pytest.fixture
def urtest(uranium_scenario: UraniumTest) -> UraniumTest:
    """Provide the transitional scenario API to tests not yet rewritten."""

    return uranium_scenario


@pytest.fixture
def ats(uranium_scenario: UraniumTest) -> Iterator[ATS]:
    """Provide one configured Traffic Server with fixture-owned cleanup."""

    service = ATS(uranium_scenario)
    try:
        yield service
    finally:
        service.close()


@pytest.fixture
def curl(uranium_scenario: UraniumTest) -> Curl:
    """Provide a curl client rooted in this test's sandbox."""

    return Curl(Path(uranium_scenario.RunDirectory))


def _partition_items(items: list[pytest.Item], predicate: Callable[[pytest.Item],
                                                                   bool]) -> tuple[list[pytest.Item], list[pytest.Item]]:
    """Split collected items while preserving their order."""

    selected = []
    rejected = []
    for item in items:
        (selected if predicate(item) else rejected).append(item)
    return selected, rejected


def _matches_filter(item: pytest.Item, patterns: list[str]) -> bool:
    """Match procedural basenames, direct replay names, and pytest node IDs."""

    names = {item.name, item.path.name, item.nodeid}
    names.update({name.removesuffix(suffix) for name in names for suffix in (".py", ".test.yaml", ".test.yml")})
    names.update({name.removeprefix("test_") for name in names})
    return any(fnmatch.fnmatch(name, pattern) for name in names for pattern in patterns)


def _is_below_uranium_directory(item: pytest.Item, directories: set[str]) -> bool:
    """Return whether an item is in a curl-UDS-incompatible directory."""

    parts = item.path.parts
    try:
        uranium_index = parts.index("uranium_tests")
    except ValueError:
        return False
    return uranium_index + 1 < len(parts) and parts[uranium_index + 1] in directories


def _is_serial_test(test_path: Path) -> bool:
    """Read the existing list of tests that require exclusive execution."""

    tests_root = Path(__file__).resolve().parents[1]
    try:
        relative = test_path.resolve().relative_to(tests_root / "uranium_tests").as_posix()
    except ValueError:
        return False
    serial_file = tests_root / "serial_tests.txt"
    entries = {line.strip() for line in serial_file.read_text().splitlines() if line.strip() and not line.lstrip().startswith("#")}
    return relative in entries
