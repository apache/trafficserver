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


@pytest.mark.parametrize("stop_after_client", [None, False, True])
def test_origin_shutdown_precedes_metrics_when_requested(tmp_path: Path, stop_after_client: bool | None) -> None:
    """Preserve intentional origin-disconnect checks without changing default teardown.

    :param tmp_path: Sandbox stand-in.
    :param stop_after_client: Whether this replay requests origin shutdown before metrics, or None to omit the key.
    """
    replay = Mock(spec=ReplayTest)
    server_config = {} if stop_after_client is None else {"stop_after_client": stop_after_client}
    replay.spec = SimpleNamespace(urtest={"server": server_config, "client": {}})
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

    replay.runtime.prepare_sandbox.assert_called_once_with(tmp_path, parent=tmp_path.parent)
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
        assert yaml.safe_load((paths["config"] / "ssl_multicert.yaml").read_text()) == document
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
        ]
    )
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


def test_audited_replay_expectations_have_explicit_verification_rules() -> None:
    """Keep the audited migration scenarios and their external replays verifiable.

    These scenarios previously used literal URL, header, or body expectations
    without enabling strict verification. Other master scenarios need a
    separate audit before this inventory can cover the entire suite.
    """

    test_root = Path(__file__).parents[3] / "uranium_tests"
    failures = []
    manifests = (
        "at_headers/at_headers.test.yaml",
        "cache/replay/cache-control-basic.test.yaml",
        "h2/replay/h2_origin_cert_reverify.test.yaml",
        "h2/replay/h2_origin_cert_reverify_permissive.test.yaml",
        "logging/replay/pqsi_pqsp.test.yaml",
        "pluginTest/cache_range_requests/cache_range_requests_cachekey_global.test.yaml",
        "pluginTest/cookie_remap/cookie_remap.test.yaml",
        "pluginTest/lua/lua_api.test.yaml",
        "pluginTest/prefetch/prefetch_query_path_traversal.test.yaml",
        "pluginTest/redirect_limit/redirect_limit.test.yaml",
        "pluginTest/slice/replay/slice_regex.test.yaml",
        "pluginTest/xdebug/x_remap/x_remap.test.yaml",
        "remap/replay/regex_map.test.yaml",
        "session_sharing/session_match.test.yaml",
        "url/uri.test.yaml",
    )
    pending = [test_root / manifest for manifest in manifests]
    assert pending, "The audited replay manifest inventory is empty"
    for path in pending:
        assert path.is_file(), f"Missing audited replay manifest: {path}"
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
                referenced = sorted(replay.rglob("*.yaml")) if replay.is_dir() else [replay]
                assert referenced, f"Empty {role} replay directory referenced by {path}: {replay}"
                pending.extend(referenced)
        for session in document.get("sessions", []):
            for transaction in session.get("transactions", []):
                for direction in ("proxy-request", "proxy-response"):
                    expected = transaction.get(direction) or {}
                    context = f"{path.relative_to(test_root)}: {direction}"
                    if isinstance(expected.get("url"), str):
                        failures.append(f"{context} URL has no component verification rules")
                    for field in (expected.get("headers") or {}).get("fields") or []:
                        if len(field) == 2 and not isinstance(field[1], dict):
                            failures.append(f"{context} header {field[0]} has no verification rule")
                    content = expected.get("content") or {}
                    if "data" in content and "verify" not in content:
                        failures.append(f"{context} literal body has no verification rule")
    assert not failures, "\n".join(failures)
