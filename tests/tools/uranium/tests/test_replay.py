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
"""Unit tests for direct Uranium replay execution."""

from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, call
import subprocess

import pytest
import yaml

from tools.uranium.replay import ReplayTest


@pytest.mark.parametrize("stop_after_client", [False, True])
def test_origin_shutdown_precedes_metrics_when_requested(tmp_path: Path, stop_after_client: bool) -> None:
    """Preserve intentional origin-disconnect checks without changing default teardown.

    :param tmp_path: Sandbox stand-in.
    :param stop_after_client: Whether this replay requests origin shutdown before metrics.
    """
    replay = Mock(spec=ReplayTest)
    replay.spec = SimpleNamespace(urtest={"server": {"stop_after_client": stop_after_client}, "client": {}})
    replay.runtime = Mock()
    replay.runtime.requirement_failure.return_value = None
    replay.sandbox = tmp_path
    replay._sandbox_parent = tmp_path.parent
    server = replay._start_verifier_server.return_value
    replay.processes = [server]
    replay._temporary_directories = []
    order = Mock()
    order.attach_mock(replay._validate_background_processes, "validate")
    order.attach_mock(server.stop, "stop")
    order.attach_mock(replay._check_metrics, "metrics")

    ReplayTest.run(replay)

    expected = [call.validate(server, replay._start_ats.return_value)]
    if stop_after_client:
        expected.append(call.stop())
    expected.extend([call.metrics(), call.stop()])
    assert order.mock_calls == expected


@pytest.mark.parametrize("custom_multicert", [False, True])
def test_tls_certificate_paths_use_the_sandbox(tmp_path: Path, custom_multicert: bool) -> None:
    """Custom multicert documents use the same isolated TLS paths as defaults.

    :param tmp_path: Temporary directory containing dummy TLS inputs.
    :param custom_multicert: Whether the test supplies a multicert document.
    """
    tools = tmp_path / "tools"
    (tools / "ssl").mkdir(parents=True)
    for name in ("server.pem", "server.key"):
        (tools / "ssl" / name).write_text("test TLS material")
    paths = {"ssl": tmp_path / "sandbox-ssl", "config": tmp_path / "config"}
    for path in paths.values():
        path.mkdir()
    document = {"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "custom.pem", "ssl_key_name": "custom.key"}]}
    config = {"ssl_multicert_yaml": document} if custom_multicert else {}
    records = {}
    replay = object.__new__(ReplayTest)
    replay.runtime = SimpleNamespace(test_tools=tools)
    replay._configure_tls(config, records, paths)
    assert records["ssl"]["server"]["cert"]["path"] == str(paths["ssl"])
    assert records["ssl"]["server"]["private_key"]["path"] == str(paths["ssl"])
    if custom_multicert:
        assert (paths["config"] /
                "ssl_multicert.config").read_text() == ("dest_ip=* ssl_cert_name=custom.pem ssl_key_name=custom.key\n")
    else:
        assert (paths["ssl"] / "server.pem").read_text() == "test TLS material"


def test_metric_check_retries_until_the_value_matches(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Allow an asynchronously updated metric to reach its expected value.

    :param monkeypatch: pytest fixture for replacing process and sleep calls.
    :param tmp_path: Stand-in ATS binary directory.
    """

    replay = object.__new__(ReplayTest)
    replay.spec = SimpleNamespace(urtest={"ats": {"metric_checks": [{"metric": "proxy.process.test", "value": 2, "delay": 0}]}})
    replay.ats_paths = {"bin": tmp_path}
    replay.ats_environment = {}
    results = iter(
        [
            subprocess.CompletedProcess([], 0, "proxy.process.test 1\n", ""),
            subprocess.CompletedProcess([], 0, "proxy.process.test 2\n", ""),
        ])
    calls = []

    def read_metric(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
        """Return the next simulated traffic_ctl result.

        :param args: Positional arguments passed to subprocess.run.
        :param kwargs: Keyword arguments passed to subprocess.run.
        """

        calls.append((args, kwargs))
        return next(results)

    monkeypatch.setattr("tools.uranium.replay.subprocess.run", read_metric)
    monkeypatch.setattr("tools.uranium.replay.time.sleep", lambda _seconds: None)

    replay._check_metrics()

    assert len(calls) == 2


def test_missing_diags_log_has_an_assertion_message(tmp_path: Path) -> None:
    """Report a missing ATS diagnostic log as a test assertion.

    :param tmp_path: Stand-in ATS log directory without a diags.log file.
    """

    replay = object.__new__(ReplayTest)
    replay.spec = SimpleNamespace(urtest={"ats": {}})
    replay.ats_paths = {"log": tmp_path}

    with pytest.raises(AssertionError, match="ATS diagnostic log does not exist"):
        replay._validate_ats_logs()


def test_replay_expectation_audit_accepts_symlinked_checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Canonicalize both sides when reporting paths from a symlinked checkout.

    :param tmp_path: Directory containing the symbolic checkout path.
    :param monkeypatch: Fixture restoring this module's source path.
    """

    checkout = tmp_path / "checkout"
    checkout.symlink_to(Path(__file__).parents[4], target_is_directory=True)
    monkeypatch.setitem(globals(), "__file__", str(checkout / "tests/tools/uranium/tests/test_replay.py"))
    test_replay_expectations_have_explicit_verification_rules()


def test_replay_expectations_have_explicit_verification_rules() -> None:
    """Prevent inert URL, header, and literal-body expectations without strict mode."""

    test_root = (Path(__file__).parents[3] / "uranium_tests").resolve()
    failures = []
    pending = sorted(test_root.rglob("*.test.yaml"))
    assert pending, f"No collected replay manifests found under {test_root}"
    visited = set()
    while pending:
        path = pending.pop().resolve()
        if path in visited:
            continue
        visited.add(path)
        document = yaml.safe_load(path.read_text())
        for role in ("server", "client"):
            reference = document.get("urtest", {}).get(role, {}).get("replay")
            if reference:
                replay = (path.parent / reference).resolve()
                assert replay.exists(), f"Missing {role} replay referenced by {path}: {replay}"
                pending.extend(sorted(replay.rglob("*.yaml")) if replay.is_dir() else [replay])
        for session in document.get("sessions", []):
            for transaction in session.get("transactions", []):
                for direction in ("proxy-request", "proxy-response"):
                    expected = transaction.get(direction, {})
                    context = f"{path.relative_to(test_root)}: {direction}"
                    if isinstance(expected.get("url"), str):
                        failures.append(f"{context} URL has no component verification rules")
                    for field in expected.get("headers", {}).get("fields", []):
                        if len(field) == 2 and not isinstance(field[1], dict):
                            failures.append(f"{context} header {field[0]} has no verification rule")
                    content = expected.get("content", {})
                    if "data" in content and "verify" not in content:
                        failures.append(f"{context} literal body has no verification rule")
    assert not failures, "\n".join(failures)
