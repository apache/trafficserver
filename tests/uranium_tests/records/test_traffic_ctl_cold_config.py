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

import yaml

from tools.uranium.services import ATS, ATSFactory, CommandResult


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure records used by append, update, and typed-value cases.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.accept_threads": 1,
            "proxy.config.cache.limits.http.max_alts": 5,
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http|dns",
        })
    return ats


def traffic_ctl(*arguments: str, _ats: ATS) -> CommandResult:
    """Run traffic_ctl and require success.

    :param _ats: Test-local ats configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _ats.traffic_ctl(*arguments)
    assert result.returncode == 0, result.output
    return result


def assert_cold_value(record: str, value: str, *, _ats: ATS) -> None:
    """Read one cold value back from records.yaml.

    :param _ats: Test-local ats configured by the test.
    :param record: Record used by this test step.
    :param value: Value used by this test step.
    """

    result = traffic_ctl("config", "get", record, "--cold", _ats=_ats)
    assert f"{record}: {value}" in result.stdout


def test_traffic_ctl_cold_config(ats_factory: ATSFactory) -> None:
    """traffic_ctl cold operations preserve nested records.yaml values.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    traffic_ctl("config", "set", "proxy.config.diags.debug.tags", "rpc", "--cold", _ats=_ats)
    assert_cold_value("proxy.config.diags.debug.tags", "rpc", _ats=_ats)

    traffic_ctl("config", "set", "proxy.config.diags.debug.tags", "http", "-u", "-c", _ats=_ats)
    assert_cold_value("proxy.config.diags.debug.tags", "http", _ats=_ats)

    traffic_ctl("config", "set", "proxy.config.cache.limits.http.max_alts", "1", "-t", "int", "-c", _ats=_ats)
    assert_cold_value("proxy.config.cache.limits.http.max_alts", "1", _ats=_ats)

    for filename, update in (("new_records.yaml", True), ("new_records2.yaml", False)):
        path = _ats.config_directory / filename
        arguments = [
            "config",
            "set",
            "proxy.config.cache.limits.http.max_alts",
            "3",
        ]
        if update:
            arguments.append("-u")
        arguments.extend(["-c", str(path)])
        traffic_ctl(*arguments, _ats=_ats)
        document = yaml.safe_load(path.read_text())
        assert document["records"]["cache"]["limits"]["http"]["max_alts"] == 3

    records_file = _ats.config_directory / "records.yaml"
    result = traffic_ctl("config", "get", "-c", str(records_file), "proxy.config.diags.debug.tags", _ats=_ats)
    assert "proxy.config.diags.debug.tags: http" in result.stdout

    result = traffic_ctl(
        "config",
        "get",
        "-c",
        str(records_file),
        "proxy.config.diags.debug.tags",
        "proxy.config.cache.limits.http.max_alts",
        _ats=_ats)
    assert "proxy.config.diags.debug.tags: http" in result.stdout
    assert "proxy.config.cache.limits.http.max_alts: 1" in result.stdout

    result = traffic_ctl("config", "get", f"--cold={records_file}", "proxy.config.diags.debug.tags", _ats=_ats)
    assert "proxy.config.diags.debug.tags: http" in result.stdout

    new_records = _ats.config_directory / "new_records3.yaml"
    traffic_ctl("config", "set", "-c", str(new_records), "proxy.config.cache.limits.http.max_alts", "3", _ats=_ats)
    document = yaml.safe_load(new_records.read_text())
    assert document["records"]["cache"]["limits"]["http"]["max_alts"] == 3

    repeated_cases = (
        (f"--cold={records_file}", f"--cold={records_file}"),
        ("-c", str(records_file), "-c", str(records_file)),
        ("-c", str(records_file), f"--cold={records_file}"),
    )
    for cold_arguments in repeated_cases:
        result = _ats.traffic_ctl(
            "config",
            "get",
            *cold_arguments,
            "proxy.config.diags.debug.tags",
        )
        assert result.returncode == 64, result.output
        assert "at most one argument expected by --cold" in result.output

    for spelling in ("-c", "--cold"):
        result = _ats.traffic_ctl(
            "config",
            "get",
            spelling,
            "",
            "proxy.config.diags.debug.tags",
        )
        assert result.returncode == 64, result.output
        assert f"missing argument for '{spelling}'" in result.output

    result = _ats.traffic_ctl(
        "config",
        "set",
        "-c",
        "",
        "proxy.config.cache.limits.http.max_alts",
        "9",
    )
    assert result.returncode == 64, result.output
    assert "missing argument for '-c'" in result.output

    result = _ats.traffic_ctl(
        "config",
        "set",
        "-c",
        "proxy.config.cache.limits.http.max_alts",
        "9",
    )
    assert result.returncode == 64, result.output
    assert "2 argument(s) expected by set" in result.output
