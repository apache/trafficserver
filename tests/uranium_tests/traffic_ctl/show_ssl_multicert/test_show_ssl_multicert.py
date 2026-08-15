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


class ShowSSLMulticertScenario:
    """Verify traffic_ctl renders ssl_multicert configuration as YAML and JSON."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._source = Path(__file__).parent
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS with the default Uranium certificate."""

        ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
        ats.add_default_ssl_files()
        return ats

    def verify_output(self, option: str | None, gold: str) -> None:
        """Run the show command and compare its selected serialization."""

        arguments = ["config", "ssl-multicert", "show"]
        if option is not None:
            arguments.append(option)
        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stdout, self._source / "gold" / gold)

    def run(self) -> None:
        """Exercise default, long, and short output-format options."""

        self._ats.start()
        for option in (None, "--yaml", "-y"):
            self.verify_output(option, "show_yaml.gold")
        for option in ("--json", "-j"):
            self.verify_output(option, "show_json.gold")


def test_show_ssl_multicert(ats_factory: ATSFactory) -> None:
    """ssl-multicert show supports its YAML and JSON spellings."""

    ShowSSLMulticertScenario(ats_factory).run()
