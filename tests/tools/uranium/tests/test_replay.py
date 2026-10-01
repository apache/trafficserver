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
import subprocess

import pytest
import yaml

from tools.uranium.replay import ReplayTest


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
