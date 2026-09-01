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

from tools.uranium.services import ATS, ATSFactory, assert_matches_gold


class PluginConfigConversionScenario:
    """Verify traffic_ctl converts legacy plugin.config syntax to YAML."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._source = Path(__file__).parent
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Create the ATS environment used to invoke traffic_ctl."""

        return ats_factory.create("ts", enable_cache=False)

    def convert(self, source: str, gold: str, *options: str, output: str = "-") -> None:
        """Convert one input and compare it with its wildcard gold file."""

        result = self._ats.traffic_ctl(
            "config",
            "convert",
            "plugin_config",
            *options,
            str(self._source / "legacy_config" / source),
            output,
        )
        assert result.returncode == 0, result.output
        actual = result.stdout if output == "-" else (self._ats.run_directory / output).read_text()
        assert_matches_gold(actual, self._source / "gold" / gold)

    def run(self) -> None:
        """Exercise ordinary, disabled, quoted, file, and filtered output."""

        self._ats.start()
        self.convert("basic.config", "basic.yaml")
        self.convert("commented.config", "commented.yaml")
        self.convert("quoted.config", "quoted.yaml")
        self.convert("basic.config", "basic.yaml", output="generated.yaml")
        self.convert("commented.config", "skip_disabled.yaml", "--skip-disabled")


def test_convert_plugin_config(ats_factory: ATSFactory) -> None:
    """traffic_ctl converts all supported plugin.config forms."""

    PluginConfigConversionScenario(ats_factory).run()
