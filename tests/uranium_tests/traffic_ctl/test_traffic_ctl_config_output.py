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


class ConfigOutputScenario:
    """Verify traffic_ctl configuration output and reset operations."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._gold = Path(__file__).parent / "gold"
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure values used by get, match, and diff output."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.udp.threads": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc",
                "proxy.config.diags.debug.throttling_interval_msec": 0,
            })
        return ats

    def command(self, *arguments: str, expected: int = 0) -> CommandResult:
        """Run traffic_ctl and validate its exit status."""

        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == expected, result.output
        return result

    def assert_text(self, expected: str, *arguments: str) -> None:
        """Require exact stdout for one command."""

        result = self.command(*arguments)
        assert result.stdout == expected + ("\n" if expected else "")

    def assert_gold(self, gold: str, *arguments: str) -> None:
        """Compare one command with a wildcard gold file."""

        result = self.command(*arguments)
        assert_matches_gold(result.stdout, self._gold / gold)

    def verify_get_match_diff_and_describe(self) -> None:
        """Verify each read-only configuration output mode."""

        self.assert_gold("t1_yaml.gold", "config", "get", "proxy.config.diags.debug.tags", "--records")
        self.assert_text("proxy.config.diags.debug.enabled: 1", "config", "get", "proxy.config.diags.debug.enabled")
        self.assert_text(
            "proxy.config.diags.debug.tags: rpc # default http|dns",
            "config",
            "get",
            "proxy.config.diags.debug.tags",
            "--default",
        )
        self.assert_gold("t2_yaml.gold", "config", "get", "proxy.config.diags.debug.tags", "--records", "--default")
        self.assert_gold(
            "t3_yaml.gold",
            "config",
            "get",
            "proxy.config.diags.debug.tags",
            "proxy.config.diags.debug.enabled",
            "proxy.config.diags.debug.throttling_interval_msec",
            "--records",
            "--default",
        )
        self.assert_gold("match.gold", "config", "match", "threads", "--default")
        self.assert_gold("t4_yaml.gold", "config", "match", "diags.logfile", "--records")
        result = self.command("config", "diff")
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

        result = self.command("config", "diff", "--records")
        records = yaml.safe_load(result.stdout)["records"]
        assert records["config_update_interval_ms"] == 20
        assert records["diags"]["debug"]["enabled"] == 1
        assert records["diags"]["debug"]["tags"] == "rpc"
        assert records["http"]["wait_for_cache"] == 1
        assert records["udp"]["threads"] == 1
        self.assert_gold("describe.gold", "config", "describe", "proxy.config.http.server_ports")

    def set_record(self, record: str, value: str) -> None:
        """Set one runtime record and require success."""

        self.command("config", "set", record, value)

    def assert_debug_tags(self, value: str) -> None:
        """Verify the current debug tag expression."""

        self.assert_text(f"proxy.config.diags.debug.tags: {value}", "config", "get", "proxy.config.diags.debug.tags")

    def verify_reset(self) -> None:
        """Verify dotted, partial, all-record, and YAML-style reset paths."""

        reset_message = (
            "Set proxy.config.diags.debug.tags, please wait 10 seconds for traffic server to sync "
            "configuration, restart is not required")
        self.assert_text(reset_message, "config", "reset", "proxy.config.diags.debug.tags")
        self.assert_debug_tags("http|dns")

        self.set_record("proxy.config.diags.debug.tags", "rpc")
        result = self.command("config", "reset", "proxy.config.diags")
        assert "Set proxy.config.diags.debug.tags" in result.stdout
        assert "Set proxy.config.diags.debug.enabled" in result.stdout
        self.assert_debug_tags("http|dns")

        self.set_record("proxy.config.diags.debug.tags", "rpc")
        self.command("config", "reset", "records")
        self.assert_text("", "config", "diff")
        self.assert_debug_tags("http|dns")

        self.set_record("proxy.config.diags.debug.tags", "yaml_test")
        self.assert_text(reset_message, "config", "reset", "records.diags.debug.tags")
        self.assert_debug_tags("http|dns")

        self.set_record("proxy.config.diags.debug.tags", "yaml_partial_test")
        self.set_record("proxy.config.diags.debug.enabled", "1")
        result = self.command("config", "reset", "records.diags")
        assert "Set proxy.config.diags.debug.tags" in result.stdout
        assert "Set proxy.config.diags.debug.enabled" in result.stdout
        self.assert_debug_tags("http|dns")

        self.command("config", "get", "invalid.should.set.the.exit.code.to.2", expected=2)

    def run(self) -> None:
        """Exercise configuration display and mutation commands."""

        self._ats.start()
        self.verify_get_match_diff_and_describe()
        self.verify_reset()


def test_traffic_ctl_config_output(ats_factory: ATSFactory) -> None:
    """traffic_ctl formats configuration output and resets values correctly."""

    ConfigOutputScenario(ats_factory).run()
