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
"""Unit tests for managed process behavior."""

from pathlib import Path
import re
import sys
from types import SimpleNamespace

import pytest

from tools.uranium.process import ManagedProcess, ProcessError
from tools.uranium.replay import ReplayTest
from tools.uranium.services.process_service import ProcessService


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


def test_explicit_stream_expectations_accumulate_and_validate(tmp_path: Path) -> None:
    """Apply multiple explicit regex expectations with caller-supplied flags."""

    process = ManagedProcess(
        "client",
        [sys.executable, "-c", "print('READY=42')"],
        tmp_path,
        test_directory=tmp_path,
    )
    service = ProcessService(process)
    service.stdout.contains(
        r"^ready=[0-9]+$",
        "The client should report its numeric ready marker.",
        reflags=re.IGNORECASE | re.MULTILINE,
    )
    service.stdout.excludes("failure", "The client should not report a failure.")

    result = service.run(5)

    assert result.returncode == 0
    assert len(service.stdout.expectations) == 2


def test_explicit_stream_expectation_failure_reports_explanation(tmp_path: Path) -> None:
    """Report the author-supplied explanation when an expectation fails."""

    process = ManagedProcess("client", [sys.executable, "-c", "print('actual')"], tmp_path)
    service = ProcessService(process)
    service.stdout.contains("expected", "The expected marker should be present.")

    with pytest.raises(AssertionError, match="The expected marker should be present"):
        service.run(5)


def test_process_service_waits_for_live_output(tmp_path: Path) -> None:
    """Poll output from a running support service until its marker arrives.

    :param tmp_path: Temporary directory containing captured process output.
    """

    process = ManagedProcess(
        "server",
        [sys.executable, "-c", "import time; time.sleep(0.2); print('ready', flush=True); time.sleep(1)"],
        tmp_path,
    )
    service = ProcessService(process)
    service.start()

    assert "ready" in service.wait_for_output(r"^ready$", timeout=2)
    service.stop()


def test_regex_stream_expectations_require_explanations(tmp_path: Path) -> None:
    """Reject contains and excludes declarations without useful explanations."""

    service = ProcessService(ManagedProcess("client", ["true"], tmp_path))

    with pytest.raises(ValueError, match="contains.*non-empty explanation"):
        service.stdout.contains("value", "")
    with pytest.raises(ValueError, match="excludes.*non-empty explanation"):
        service.stderr.excludes("value", "   ")


def test_gold_expectation_and_reset_preserve_stream_identity(tmp_path: Path) -> None:
    """Match wildcard gold output after resetting an earlier expectation."""

    (tmp_path / "output.gold").write_text("prefix``suffix\n")
    process = ManagedProcess(
        "client",
        [sys.executable, "-c", "print('prefix variable suffix')"],
        tmp_path / "run",
        test_directory=tmp_path,
    )
    service = ProcessService(process)
    stream = service.stdout
    stream_path = stream.path
    stream.contains("discarded", "This expectation should be removed.")

    stream.reset()
    stream.matches_gold("output.gold")
    service.run(5)

    assert service.stdout is stream
    assert service.stdout.path == stream_path
    assert len(service.stdout.expectations) == 1


def test_process_expectation_properties_are_read_only(tmp_path: Path) -> None:
    """Fail loudly when assignment tries to replace expectation state."""

    service = ProcessService(ManagedProcess("client", ["true"], tmp_path))

    with pytest.raises(AttributeError, match=r"stdout\.contains"):
        service.stdout = object()
    with pytest.raises(AttributeError, match=r"stderr\.contains"):
        service.stderr = object()
    with pytest.raises(TypeError, match=r"stdout\.contains"):
        service.stdout += object()


def test_expected_return_codes_use_explicit_api(tmp_path: Path) -> None:
    """Validate explicit exit statuses and reject direct assignment."""

    process = ManagedProcess("client", [sys.executable, "-c", "raise SystemExit(7)"], tmp_path)
    service = ProcessService(process)

    assert service.return_codes == (0,)
    service.expect_return_codes(0, 7)
    result = service.run(5)
    assert result.returncode == 7
    assert service.return_codes == (0, 7)

    with pytest.raises(AttributeError, match="expect_return_codes"):
        service.return_codes = (7,)
    with pytest.raises(ValueError, match="at least one"):
        service.expect_return_codes()
    with pytest.raises(TypeError, match="integers"):
        service.expect_return_codes("7")  # type: ignore[arg-type]


def test_replay_return_codes_accept_procedural_sequences() -> None:
    """Preserve tuple return-code options supplied by procedural ATS tests."""

    assert tuple(ReplayTest._return_codes({"return_code": (0, -2)})) == (0, -2)


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
