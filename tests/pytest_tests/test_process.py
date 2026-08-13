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
"""Unit tests for managed process and gold-file behavior."""

from pathlib import Path
import sys

import pytest

from uranium_testkit.process import ManagedProcess, ProcessError
from uranium_testkit.replay import ReplayTest


def test_managed_process_captures_output(tmp_path: Path) -> None:
    """Capture stdout from a successful one-shot process."""

    process = ManagedProcess("client", [sys.executable, "-c", "print('ready')"], tmp_path)
    process.start()
    process.wait(5)
    assert "ready" in process.output()


def test_managed_process_reports_unexpected_status(tmp_path: Path) -> None:
    """Include captured diagnostics when a process fails."""

    process = ManagedProcess("client", [sys.executable, "-c", "print('bad'); raise SystemExit(3)"], tmp_path)
    process.start()
    with pytest.raises(ProcessError, match="status 3"):
        process.wait(5)
    assert "bad" in process.output()


def test_gold_file_wildcards_match_variable_text(tmp_path: Path) -> None:
    """Preserve AuTest's `` and {} wildcard tokens in migrated gold files."""

    expected = tmp_path / "expected.gold"
    actual = tmp_path / "actual.log"
    expected.write_text("port=`` id={} done\n")
    actual.write_text("port=43127 id=abc-123 done\n")
    ReplayTest._validate_gold(actual, expected)


def test_gold_file_difference_fails(tmp_path: Path) -> None:
    """Report a mismatch when fixed gold-file text changes."""

    expected = tmp_path / "expected.gold"
    actual = tmp_path / "actual.log"
    expected.write_text("expected\n")
    actual.write_text("actual\n")
    with pytest.raises(AssertionError, match="did not match"):
        ReplayTest._validate_gold(actual, expected)
