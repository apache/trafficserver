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

from pathlib import Path

import yaml

from tools.uranium.services import ATS, ATSFactory, CommandResult, assert_matches_gold


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure values used by get, match, and diff output.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", disable_log_rolling=False)
    ats.records.update(
        {
            "proxy.config.udp.threads": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc",
            "proxy.config.diags.debug.throttling_interval_msec": 0,
        })
    return ats


def command(*arguments: str, expected: int = 0, _ats: ATS) -> CommandResult:
    """Run traffic_ctl and validate its exit status.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    :param arguments: Arguments used by this test step.
    """

    result = _ats.traffic_ctl(*arguments)
    assert result.returncode == expected, result.output
    return result


def assert_text(expected: str, *arguments: str, _ats: ATS) -> None:
    """Require exact stdout for one command.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    :param arguments: Arguments used by this test step.
    """

    result = command(*arguments, _ats=_ats)
    assert result.stdout == expected + ("\n" if expected else "")


def assert_gold(gold: str, *arguments: str, _ats: ATS, _gold: Path) -> None:
    """Compare one command with a wildcard gold file.

    :param _ats: Test-local ats configured by the test.
    :param _gold: Test-local gold configured by the test.
    :param gold: Gold used by this test step.
    :param arguments: Arguments used by this test step.
    """

    result = command(*arguments, _ats=_ats)
    assert_matches_gold(result.stdout, _gold / gold)


def verify_get_match_diff_and_describe(*, _ats: ATS, _gold: Path) -> None:
    """Verify each read-only configuration output mode.

    :param _ats: Test-local ats configured by the test.
    :param _gold: Test-local gold configured by the test.
    """

    assert_gold("t1_yaml.gold", "config", "get", "proxy.config.diags.debug.tags", "--records", _ats=_ats, _gold=_gold)
    assert_text("proxy.config.diags.debug.enabled: 1", "config", "get", "proxy.config.diags.debug.enabled", _ats=_ats)
    assert_text(
        "proxy.config.diags.debug.tags: rpc # default http|dns",
        "config",
        "get",
        "proxy.config.diags.debug.tags",
        "--default",
        _ats=_ats)
    assert_gold("t2_yaml.gold", "config", "get", "proxy.config.diags.debug.tags", "--records", "--default", _ats=_ats, _gold=_gold)
    assert_gold(
        "t3_yaml.gold",
        "config",
        "get",
        "proxy.config.diags.debug.tags",
        "proxy.config.diags.debug.enabled",
        "proxy.config.diags.debug.throttling_interval_msec",
        "--records",
        "--default",
        _ats=_ats,
        _gold=_gold)
    assert_gold("match.gold", "config", "match", "threads", "--default", _ats=_ats, _gold=_gold)
    assert_gold("t4_yaml.gold", "config", "match", "diags.logfile", "--records", _ats=_ats, _gold=_gold)
    result = command("config", "diff", _ats=_ats)
    for record, current, default in (
        ("proxy.config.config_update_interval_ms", "20", "3000"),
        ("proxy.config.diags.debug.enabled", "1", "0"),
        ("proxy.config.diags.debug.tags", "rpc", "http|dns"),
        ("proxy.config.http.wait_for_cache", "1", "0"),
        ("proxy.config.udp.threads", "1", "0"),
    ):
        assert f"{record} has changed" in result.stdout
        assert f"Current Value: {current}" in result.stdout
        assert f"Default Value: {default}" in result.stdout

    result = command("config", "diff", "--records", _ats=_ats)
    records = yaml.safe_load(result.stdout)["records"]
    assert records["config_update_interval_ms"] == 20
    assert records["diags"]["debug"]["enabled"] == 1
    assert records["diags"]["debug"]["tags"] == "rpc"
    assert records["http"]["wait_for_cache"] == 1
    assert records["udp"]["threads"] == 1
    assert_gold("describe.gold", "config", "describe", "proxy.config.http.server_ports", _ats=_ats, _gold=_gold)


def set_record(record: str, value: str, *, _ats: ATS) -> None:
    """Set one runtime record and require success.

    :param _ats: Test-local ats configured by the test.
    :param record: Record used by this test step.
    :param value: Value used by this test step.
    """

    command("config", "set", record, value, _ats=_ats)


def assert_debug_tags(value: str, *, _ats: ATS) -> None:
    """Verify the current debug tag expression.

    :param _ats: Test-local ats configured by the test.
    :param value: Value used by this test step.
    """

    assert_text(f"proxy.config.diags.debug.tags: {value}", "config", "get", "proxy.config.diags.debug.tags", _ats=_ats)


def verify_reset(*, _ats: ATS) -> None:
    """Verify dotted, partial, all-record, and YAML-style reset paths.

    :param _ats: Test-local ats configured by the test.
    """

    reset_message = (
        "Set proxy.config.diags.debug.tags, please wait 10 seconds for traffic server to sync "
        "configuration, restart is not required")
    assert_text(reset_message, "config", "reset", "proxy.config.diags.debug.tags", _ats=_ats)
    assert_debug_tags("http|dns", _ats=_ats)

    set_record("proxy.config.diags.debug.tags", "rpc", _ats=_ats)
    result = command("config", "reset", "proxy.config.diags", _ats=_ats)
    assert "Set proxy.config.diags.debug.tags" in result.stdout
    assert "Set proxy.config.diags.debug.enabled" in result.stdout
    assert_debug_tags("http|dns", _ats=_ats)

    set_record("proxy.config.diags.debug.tags", "rpc", _ats=_ats)
    command("config", "reset", "records", _ats=_ats)
    assert_text("", "config", "diff", _ats=_ats)
    assert_debug_tags("http|dns", _ats=_ats)

    set_record("proxy.config.diags.debug.tags", "yaml_test", _ats=_ats)
    assert_text(reset_message, "config", "reset", "records.diags.debug.tags", _ats=_ats)
    assert_debug_tags("http|dns", _ats=_ats)

    set_record("proxy.config.diags.debug.tags", "yaml_partial_test", _ats=_ats)
    set_record("proxy.config.diags.debug.enabled", "1", _ats=_ats)
    result = command("config", "reset", "records.diags", _ats=_ats)
    assert "Set proxy.config.diags.debug.tags" in result.stdout
    assert "Set proxy.config.diags.debug.enabled" in result.stdout
    assert_debug_tags("http|dns", _ats=_ats)

    command("config", "get", "invalid.should.set.the.exit.code.to.2", expected=2, _ats=_ats)


def test_traffic_ctl_config_output(ats_factory: ATSFactory) -> None:
    """traffic_ctl formats configuration output and resets values correctly.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _gold = Path(__file__).parent / "gold"
    _ats = configure_ats(ats_factory)

    _ats.start()
    verify_get_match_diff_and_describe(_ats=_ats, _gold=_gold)
    verify_reset(_ats=_ats)
