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

import json
import re
import time

from tools.uranium.services import ATS, ATSFactory, CommandResult


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Enable RPC and configuration diagnostics for reload operations.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.udp.threads": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config",
            "proxy.config.diags.debug.throttling_interval_msec": 0,
        })
    return ats


def command(*arguments: str, expected: int | set[int] = 0, _ats: ATS) -> CommandResult:
    """Run traffic_ctl and validate its exit status.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    :param arguments: Arguments used by this test step.
    """

    result = _ats.traffic_ctl(*arguments)
    expected_codes = {expected} if isinstance(expected, int) else expected
    assert result.returncode in expected_codes, result.output
    return result


def reload(*options: str, expected: int | set[int] = 0, _ats: ATS) -> CommandResult:
    """Schedule one configuration reload.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    :param options: Options used by this test step.
    """

    return command("config", "reload", *options, expected=expected, _ats=_ats)


def wait_for_reload(token: str, *, _ats: ATS) -> str:
    """Poll a reload token until it reaches a terminal state.

    :param _ats: Test-local ats configured by the test.
    :param token: Token used by this test step.
    """

    deadline = time.monotonic() + 15
    latest = {}
    while time.monotonic() < deadline:
        result = _ats.rpc(
            {
                "jsonrpc": "2.0",
                "id": "reload-status",
                "method": "get_reload_config_status",
                "params": {
                    "token": token
                },
            })
        assert result.returncode == 0, result.output
        latest = json.loads(result.stdout)
        assert "error" not in latest, latest
        tasks = latest.get("result", {}).get("tasks", [])
        # CLI summaries contain "0 success" even while the root is running.
        if tasks and all(task["status"] in ("success", "fail", "timeout") for task in tasks):
            assert all(task["status"] == "success" for task in tasks), latest
            return command("config", "status", "--token", token, _ats=_ats).stdout
        time.sleep(0.1)
    raise AssertionError(f"Reload {token!r} did not finish:\n{latest}")


def verify_empty_status(*, _ats: ATS) -> None:
    """Verify status diagnostics before any reload exists.

    :param _ats: Test-local ats configured by the test.
    """

    result = command("config", "status", expected={0, 2}, _ats=_ats)
    assert "No reload tasks found" in result.output
    assert "Code: 6005" in result.output

    result = command("config", "status", "--token", "test1", expected={0, 2}, _ats=_ats)
    assert "Token 'test1' not found" in result.output
    assert "Code: 6001" in result.output

    result = command("config", "status", "--count", "all", expected={0, 2}, _ats=_ats)
    assert "No reload tasks found" in result.output

    result = command("config", "status", "--token", "test1", "--count", "all", expected={0, 2}, _ats=_ats)
    assert "can't use both --token and --count" in result.output
    assert "Token 'test1' not found" in result.output


def verify_scheduling_and_tokens(*, _ats: ATS) -> None:
    """Verify generated tokens, details, custom tokens, and duplicates.

    :param _ats: Test-local ats configured by the test.
    """

    result = reload(_ats=_ats)
    assert "Reload scheduled" in result.stdout
    match = re.search(r"Reload scheduled \[([^]]+)\]", result.stdout)
    assert match is not None, result.stdout
    generated_token = match.group(1)
    assert f"traffic_ctl config reload -t {generated_token} -m" in result.stdout
    assert f"traffic_ctl config reload -t {generated_token} -s -l" in result.stdout
    wait_for_reload(generated_token, _ats=_ats)

    result = reload("--token", "show-details", "--show-details", "--initial-wait", "0.1", _ats=_ats)
    assert "Reload scheduled" in result.stdout
    assert "Waiting for details" in result.stdout
    assert "Reload [" in result.stdout
    assert "Reload [success]" in wait_for_reload("show-details", _ats=_ats)

    token = "testtoken_1234"
    result = reload("--token", token, _ats=_ats)
    assert f"Reload scheduled [{token}]" in result.stdout
    wait_for_reload(token, _ats=_ats)
    result = command("config", "status", "--token", token, _ats=_ats)
    assert "success" in result.stdout
    assert token in result.stdout

    result = reload("--token", token, expected=2, _ats=_ats)
    assert f"Token '{token}' already in use" in result.stdout
    assert f"traffic_ctl config status -t {token}" in result.stdout


def verify_file_and_forced_reload(*, _ats: ATS) -> None:
    """Verify changed-file details and a forced reload.

    :param _ats: Test-local ats configured by the test.
    """

    (_ats.config_directory / "ip_allow.yaml").touch()
    token = "reload_ip_allow"
    result = reload("--token", token, "--show-details", "--initial-wait", "0.1", _ats=_ats)
    assert token in result.stdout
    details = wait_for_reload(token, _ats=_ats)
    assert "Reload [success]" in details
    assert "ip_allow.yaml" in details

    token = "force_reload"
    result = reload("--force", "--token", token, _ats=_ats)
    assert "Reload scheduled" in result.stdout
    wait_for_reload(token, _ats=_ats)


def verify_inline_data(*, _ats: ATS) -> None:
    """Verify invalid inline and multi-key data do not leave a stuck task.

    :param _ats: Test-local ats configured by the test.
    """

    result = reload("--force", "--data", "unknown_cfg: {foo: bar}", expected={0, 1, 2}, _ats=_ats)
    assert re.search(r"not registered|No configs were scheduled", result.output, re.IGNORECASE)

    token = "after_inline_test"
    result = reload("--token", token, _ats=_ats)
    assert f"Reload scheduled [{token}]" in result.stdout
    wait_for_reload(token, _ats=_ats)

    multi_config = _ats.config_directory / "multi_test.yaml"
    multi_config.write_text("config_a:\n  foo: bar\nconfig_b:\n  baz: qux\n")
    result = reload("--force", "--data", f"@{multi_config}", expected={0, 1, 2}, _ats=_ats)
    assert re.search(r"not registered|No configs were scheduled|error", result.output, re.IGNORECASE)

    result = reload("--force", "--data", "test_config: {key: value}", expected={0, 1, 2}, _ats=_ats)
    assert re.search(r"not registered|No configs were scheduled|scheduled", result.output, re.IGNORECASE)


def verify_exit_codes(*, _ats: ATS) -> None:
    """Verify successful, monitored, and duplicate-token exit codes.

    :param _ats: Test-local ats configured by the test.
    """

    token = "exit_code_ok"
    reload("--token", token, _ats=_ats)
    wait_for_reload(token, _ats=_ats)

    result = reload(
        "--token",
        "exit_code_monitor_ok",
        "--monitor",
        "--initial-wait",
        "0.1",
        "--refresh-int",
        "0.1",
        "--timeout",
        "15s",
        _ats=_ats)
    assert result.returncode == 0
    reload("--token", token, expected=2, _ats=_ats)


def test_traffic_ctl_config_reload(ats_factory: ATSFactory) -> None:
    """traffic_ctl reloads configs and reports stable status and exit codes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    verify_empty_status(_ats=_ats)
    verify_scheduling_and_tokens(_ats=_ats)
    verify_file_and_forced_reload(_ats=_ats)
    verify_inline_data(_ats=_ats)
    verify_exit_codes(_ats=_ats)
