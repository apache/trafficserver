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
"""Native scenario shared by the continuation scheduling API tests."""

from pathlib import Path
import subprocess
import sys
import time
import re

from tools.uranium.services import ATS, ATSFactory, assert_matches_gold


class ContScheduleScenario:
    """Load one scheduling mode and validate its thread-affinity diagnostics."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        *,
        directory: Path,
        mode: str,
        gold_name: str,
        entire_pool_minimum: int | None = None,
    ) -> None:
        self._ats_factory = ats_factory
        self._mode = mode
        self._directory = directory
        self._gold = self._directory / "gold" / gold_name
        self._entire_pool_minimum = entire_pool_minimum

    def configure_ats(self) -> ATS:
        """Configure the test plugin and deterministic thread-pool sizes."""

        ats = self._ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.exec_thread.autoconfig.enabled": 0,
                "proxy.config.exec_thread.autoconfig.scale": 1.5,
                "proxy.config.exec_thread.limit": 32,
                "proxy.config.accept_threads": 1,
                "proxy.config.task_threads": 2,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "TSContSchedule_test",
            })
        ats.copy_custom_plugin("{AtsTestPluginsDir}/cont_schedule.so")
        ats.plugin_config.add_line(f"cont_schedule.so {self._mode}")
        return ats

    def collect_output(self, ats: ATS) -> str:
        """Return either raw plugin output or the entire-pool summary."""

        # The recurring cases emit their third callback just under three
        # seconds after startup; leave time for the final line to be flushed.
        time.sleep(5)
        assert ats.traffic_out.exists()
        content = ats.traffic_out.read_text(errors="replace")
        if self._entire_pool_minimum is None:
            return content
        result = subprocess.run(
            [
                sys.executable,
                self._directory / "entire_pool.py",
                ats.traffic_out,
                "ET_NET",
                "32",
                str(self._entire_pool_minimum),
            ],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

    def run(self) -> None:
        """Start ATS and compare the scheduling diagnostics with the gold output."""

        ats = self.configure_ats()
        ats.start()
        output = self.collect_output(ats)
        assert "fail" not in output
        if self._entire_pool_minimum is not None:
            assert_matches_gold(output, self._gold)
            return

        position = 0
        for fragment in re.split(r"(?:\{\}|``)", self._gold.read_text(errors="replace")):
            if not fragment.strip():
                continue
            position = output.find(fragment, position)
            assert position >= 0, f"Missing expected scheduling diagnostic {fragment!r}:\n{output}"
            position += len(fragment)
