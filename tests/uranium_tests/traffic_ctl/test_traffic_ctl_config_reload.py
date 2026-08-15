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

import re
import time

from tools.uranium.services import ATS, ATSFactory, CommandResult


class ConfigReloadScenario:
    """Exercise traffic_ctl configuration reload scheduling and status output."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable RPC and configuration diagnostics for reload operations."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.udp.threads": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config",
                "proxy.config.diags.debug.throttling_interval_msec": 0,
            })
        return ats

    def command(self, *arguments: str, expected: int | set[int] = 0) -> CommandResult:
        """Run traffic_ctl and validate its exit status."""

        result = self._ats.traffic_ctl(*arguments)
        expected_codes = {expected} if isinstance(expected, int) else expected
        assert result.returncode in expected_codes, result.output
        return result

    def reload(self, *options: str, expected: int | set[int] = 0) -> CommandResult:
        """Schedule one configuration reload."""

        return self.command("config", "reload", *options, expected=expected)

    def wait_for_reload(self, token: str) -> str:
        """Poll a reload token until it reaches a terminal state."""

        deadline = time.monotonic() + 15
        latest = ""
        while time.monotonic() < deadline:
            result = self.command("config", "status", "--token", token, expected={0, 2})
            latest = result.output
            if "success" in latest:
                return latest
            if "failed" in latest:
                raise AssertionError(latest)
            time.sleep(0.1)
        raise AssertionError(f"Reload {token!r} did not finish:\n{latest}")

    def verify_empty_status(self) -> None:
        """Verify status diagnostics before any reload exists."""

        result = self.command("config", "status", expected={0, 2})
        assert "No reload tasks found" in result.output
        assert "Code: 6005" in result.output

        result = self.command("config", "status", "--token", "test1", expected={0, 2})
        assert "Token 'test1' not found" in result.output
        assert "Code: 6001" in result.output

        result = self.command("config", "status", "--count", "all", expected={0, 2})
        assert "No reload tasks found" in result.output

        result = self.command("config", "status", "--token", "test1", "--count", "all", expected={0, 2})
        assert "can't use both --token and --count" in result.output
        assert "Token 'test1' not found" in result.output

    def verify_scheduling_and_tokens(self) -> None:
        """Verify generated tokens, details, custom tokens, and duplicates."""

        result = self.reload()
        assert "Reload scheduled" in result.stdout
        match = re.search(r"Reload scheduled \[([^]]+)\]", result.stdout)
        assert match is not None, result.stdout
        generated_token = match.group(1)
        assert f"traffic_ctl config reload -t {generated_token} -m" in result.stdout
        assert f"traffic_ctl config reload -t {generated_token} -s -l" in result.stdout
        self.wait_for_reload(generated_token)

        result = self.reload("--token", "show-details", "--show-details", "--initial-wait", "0.1")
        assert "Reload scheduled" in result.stdout
        assert "Waiting for details" in result.stdout
        assert "Reload [success]" in result.stdout

        token = "testtoken_1234"
        result = self.reload("--token", token)
        assert f"Reload scheduled [{token}]" in result.stdout
        self.wait_for_reload(token)
        result = self.command("config", "status", "--token", token)
        assert "success" in result.stdout
        assert token in result.stdout

        result = self.reload("--token", token, expected=2)
        assert f"Token '{token}' already in use" in result.stdout
        assert f"traffic_ctl config status -t {token}" in result.stdout

    def verify_file_and_forced_reload(self) -> None:
        """Verify changed-file details and a forced reload."""

        (self._ats.config_directory / "ip_allow.yaml").touch()
        token = "reload_ip_allow"
        result = self.reload("--token", token, "--show-details", "--initial-wait", "0.1")
        assert token in result.stdout
        assert "success" in result.stdout
        assert "ip_allow.yaml" in result.stdout

        token = "force_reload"
        result = self.reload("--force", "--token", token)
        assert "Reload scheduled" in result.stdout
        self.wait_for_reload(token)

    def verify_inline_data(self) -> None:
        """Verify invalid inline and multi-key data do not leave a stuck task."""

        result = self.reload("--force", "--data", "unknown_cfg: {foo: bar}", expected={0, 1, 2})
        assert re.search(r"not registered|No configs were scheduled", result.output, re.IGNORECASE)

        token = "after_inline_test"
        result = self.reload("--token", token)
        assert f"Reload scheduled [{token}]" in result.stdout
        self.wait_for_reload(token)

        multi_config = self._ats.config_directory / "multi_test.yaml"
        multi_config.write_text("config_a:\n  foo: bar\nconfig_b:\n  baz: qux\n")
        result = self.reload("--force", "--data", f"@{multi_config}", expected={0, 1, 2})
        assert re.search(r"not registered|No configs were scheduled|error", result.output, re.IGNORECASE)

        result = self.reload("--force", "--data", "test_config: {key: value}", expected={0, 1, 2})
        assert re.search(r"not registered|No configs were scheduled|scheduled", result.output, re.IGNORECASE)

    def verify_exit_codes(self) -> None:
        """Verify successful, monitored, and duplicate-token exit codes."""

        token = "exit_code_ok"
        self.reload("--token", token)
        self.wait_for_reload(token)

        result = self.reload(
            "--token",
            "exit_code_monitor_ok",
            "--monitor",
            "--initial-wait",
            "0.1",
            "--refresh-int",
            "0.1",
            "--timeout",
            "15s",
        )
        assert result.returncode == 0
        self.reload("--token", token, expected=2)

    def run(self) -> None:
        """Run the complete configuration reload command matrix."""

        self._ats.start()
        self.verify_empty_status()
        self.verify_scheduling_and_tokens()
        self.verify_file_and_forced_reload()
        self.verify_inline_data()
        self.verify_exit_codes()


def test_traffic_ctl_config_reload(ats_factory: ATSFactory) -> None:
    """traffic_ctl reloads configs and reports stable status and exit codes."""

    ConfigReloadScenario(ats_factory).run()
