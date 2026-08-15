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

from tools.uranium.services import ATS, ATSFactory, CommandResult


class ServerDebugScenario:
    """Exercise traffic_ctl's runtime debug enable and disable operations."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Start with debug output disabled and a recognizable tag value."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "xyz",
        })
        return ats

    def traffic_ctl(self, *arguments: str, expected: int = 0) -> CommandResult:
        """Run traffic_ctl and validate its status."""

        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == expected, result.output
        return result

    def assert_record(self, record: str, value: str) -> None:
        """Verify one runtime record value."""

        result = self.traffic_ctl("config", "get", record)
        assert f"{record}: {value}" in result.stdout

    def enable(self, tags: str, *, append: bool = False) -> None:
        """Enable debug output with replacement or append semantics."""

        arguments = ["server", "debug", "enable", "--tags", tags]
        if append:
            arguments.append("--append")
        self.traffic_ctl(*arguments)

    def run(self) -> None:
        """Verify replacement, append, disable, and invalid option handling."""

        self._ats.start()
        self.enable("http")
        self.assert_record("proxy.config.diags.debug.enabled", "1")
        self.assert_record("proxy.config.diags.debug.tags", "http")

        self.traffic_ctl("server", "debug", "disable")
        self.assert_record("proxy.config.diags.debug.enabled", "0")

        self.enable("cache")
        self.assert_record("proxy.config.diags.debug.tags", "cache")
        self.enable("http", append=True)
        self.assert_record("proxy.config.diags.debug.tags", "cache|http")
        self.enable("dns", append=True)
        self.assert_record("proxy.config.diags.debug.tags", "cache|http|dns")

        self.traffic_ctl("server", "debug", "disable")
        self.assert_record("proxy.config.diags.debug.enabled", "0")
        result = self.traffic_ctl("server", "debug", "enable", "--append", expected=64)
        assert "Option '--append' requires '--tags' to be specified" in result.output


def test_traffic_ctl_server_debug(ats_factory: ATSFactory) -> None:
    """traffic_ctl updates debug records and enforces its option contract."""

    ServerDebugScenario(ats_factory).run()
