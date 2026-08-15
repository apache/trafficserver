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


class ColdConfigScenario:
    """Verify traffic_ctl reads and writes records.yaml without a reload."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure records used by append, update, and typed-value cases."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.accept_threads": 1,
                "proxy.config.cache.limits.http.max_alts": 5,
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http|dns",
            })
        return ats

    def traffic_ctl(self, *arguments: str) -> CommandResult:
        """Run traffic_ctl and require success."""

        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        return result

    def assert_cold_value(self, record: str, value: str) -> None:
        """Read one cold value back from records.yaml."""

        result = self.traffic_ctl("config", "get", record, "--cold")
        assert f"{record}: {value}" in result.stdout

    def run(self) -> None:
        """Exercise append, update, type-tag, and alternate-file writes."""

        self._ats.start()
        self.traffic_ctl("config", "set", "proxy.config.diags.debug.tags", "rpc", "--cold")
        self.assert_cold_value("proxy.config.diags.debug.tags", "rpc")

        self.traffic_ctl("config", "set", "proxy.config.diags.debug.tags", "http", "-u", "-c")
        self.assert_cold_value("proxy.config.diags.debug.tags", "http")

        self.traffic_ctl("config", "set", "proxy.config.cache.limits.http.max_alts", "1", "-t", "int", "-c")
        self.assert_cold_value("proxy.config.cache.limits.http.max_alts", "1")

        for filename, update in (("new_records.yaml", True), ("new_records2.yaml", False)):
            path = self._ats.config_directory / filename
            arguments = [
                "config",
                "set",
                "proxy.config.cache.limits.http.max_alts",
                "3",
            ]
            if update:
                arguments.append("-u")
            arguments.extend(["-c", str(path)])
            self.traffic_ctl(*arguments)
            document = yaml.safe_load(path.read_text())
            assert document["records"]["cache"]["limits"]["http"]["max_alts"] == 3


def test_traffic_ctl_cold_config(ats_factory: ATSFactory) -> None:
    """traffic_ctl cold operations preserve nested records.yaml values."""

    ColdConfigScenario(ats_factory).run()
