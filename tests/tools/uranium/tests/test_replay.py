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


@pytest.mark.parametrize("legacy", [False, True])
def test_cache_configuration_formats(tmp_path: Path, legacy: bool) -> None:
    """Write both cache formats without discarding first-match YAML rules.

    :param tmp_path: Temporary directory for serialized configurations.
    :param legacy: Whether this test supplies additive cache.config rules.
    """

    replay = Mock(spec=ReplayTest)
    replay._replace_runtime_placeholders.side_effect = lambda value, _paths: value
    document = {"cache": [{"match": {"dest_domain": "example.com"}, "action": {"cache": "never"}}]}
    config = {"cache_config": ["dest_domain=example.com action=never-cache"]} if legacy else {"cache_yaml": document}
    ReplayTest._write_ats_configs(replay, config, {"config": tmp_path})
    assert yaml.safe_load((tmp_path / "cache.yaml").read_text()) == ({"cache": []} if legacy else document)
    assert (tmp_path / "cache.config").read_text() == ("dest_domain=example.com action=never-cache\n" if legacy else "")


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
