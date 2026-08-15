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
import subprocess
import sys

from tools.uranium.services import ProceduralContext, assert_matches_gold


class RecordsToYamlScenario:
    """Verify conversion of legacy records.config files to records.yaml."""

    def __init__(self, context: ProceduralContext) -> None:
        self._directory = Path(__file__).parent
        self._run_directory = context.run_directory
        self._converter = context.runtime.repository_root / "tools/records/convert2yaml.py"

    def convert(
        self,
        source_name: str,
        output_name: str,
        *options: str,
        expected_return_code: int = 0,
    ) -> subprocess.CompletedProcess[str]:
        """Convert one source file into this scenario's sandbox."""

        result = subprocess.run(
            [
                sys.executable,
                self._converter,
                "-f",
                self._directory / "legacy_config" / source_name,
                "--output",
                self._run_directory / output_name,
                "--yaml",
                *options,
            ],
            cwd=self._run_directory,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        assert result.returncode == expected_return_code, result.stdout + result.stderr
        return result

    def assert_output(self, output_name: str, gold_name: str) -> None:
        """Compare one generated YAML document with its gold file."""

        assert_matches_gold(
            (self._run_directory / output_name).read_text(errors="replace"),
            self._directory / "gold" / gold_name,
        )

    def run(self) -> None:
        """Exercise full, renamed, invalid-override, and no-newline inputs."""

        self.convert("full_records.config", "generated1.yaml", "--mute")
        self.assert_output("generated1.yaml", "full_records.yaml")

        renamed = self.convert("old_records.config", "generated2.yaml")
        renamed_gold = (self._directory / "gold/renamed_records.gold").read_text(errors="replace").splitlines()
        assert "\n".join(renamed_gold[1:-1]) in renamed.stdout + renamed.stderr
        self.assert_output("generated2.yaml", "renamed_records.yaml")

        override_value = self.convert("override_value.config", "override-value.yaml", "-m", expected_return_code=1)
        assert (
            "We cannot continue with 'proxy.config.ssl.client.verify.server.policy' at line '3' "
            "as a value node will be overridden" in override_value.stdout)

        override_map = self.convert("override_map.config", "override-map.yaml", "-m", expected_return_code=1)
        assert (
            "We cannot continue with 'proxy.config.ssl.client.verify.server' at line '3' "
            "as an existing YAML map will be overridden." in override_map.stdout)

        self.convert("no_newline.config", "generated3.yaml", "--mute")
        self.assert_output("generated3.yaml", "no_newline.yaml")


def test_records_config_to_yaml(procedural_context: ProceduralContext) -> None:
    """The legacy converter produces the expected nested YAML records."""

    RecordsToYamlScenario(procedural_context).run()
