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
from types import SimpleNamespace

import pytest

from tools.uranium.process import ManagedProcess, ProcessError
from tools.uranium.replay import ReplayTest


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
    """Preserve the established `` and {} wildcard tokens in migrated gold files."""

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


def test_runtime_placeholders_are_replaced_in_structured_metadata(tmp_path: Path) -> None:
    """Render listener placeholders nested in structured ATS configuration."""

    replay = object.__new__(ReplayTest)
    replay.server_http_port = 12345
    replay.server_extra_http_port = 12346
    replay.server_https_port = 12347
    replay.http_port = 12348
    replay.https_port = 12349
    replay.proxy_protocol_port = 12350
    replay.proxy_protocol_https_port = 12351
    replay.dns_port = 12352
    replay.unused_port = 12353
    replay.spec = SimpleNamespace(path=tmp_path / "example.test.yaml")
    paths = {
        "root": tmp_path / "root",
        "config": tmp_path / "config",
        "log": tmp_path / "log",
        "rpc_runtime": tmp_path / "rpc",
        "storage": tmp_path / "storage",
        "ssl": tmp_path / "ssl",
    }
    sni_yaml = {
        "sni": [{
            "fqdn": "proxy.test",
            "proxy_protocol_port": "{ATS_PROXY_PROTOCOL_PORT}",
        }]
    }

    rendered = replay._replace_runtime_placeholders(sni_yaml, paths)

    assert rendered["sni"][0]["proxy_protocol_port"] == "12350"
