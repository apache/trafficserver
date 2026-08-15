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

import time

from tools.uranium.services import ATS, ATSFactory


class ParentConfigReloadScenario:
    """Reload parent.config after a file event and a dependent record update."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Create ATS with one observable parent-selection rule."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "parent_select|config",
        })
        ats.parent_config.add_line('dest_domain=example.com parent="origin.example.com:80"')
        return ats

    def wait_for_loads(self, expected: int) -> None:
        """Wait for @a expected completed parent.config loads."""

        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self._ats.diags_log.read_text(errors="replace").count("parent.config finished loading") >= expected:
                return
            time.sleep(0.1)
        raise AssertionError(f"parent.config did not finish loading {expected} times")

    def reload_touched_file(self) -> None:
        """Touch parent.config and request a normal configuration reload."""

        self._ats.parent_config.path.touch()
        result = self._ats.traffic_ctl("config", "reload")
        assert result.returncode == 0, result.output
        self.wait_for_loads(2)

    def reload_after_record_update(self) -> None:
        """Verify the registered retry-time callback reloads parent.config."""

        result = self._ats.traffic_ctl("config", "set", "proxy.config.http.parent_proxy.retry_time", "60")
        assert result.returncode == 0, result.output
        self.wait_for_loads(3)

    def run(self) -> None:
        """Exercise both ConfigRegistry reload triggers."""

        self._ats.start()
        self.wait_for_loads(1)
        self.reload_touched_file()
        self.reload_after_record_update()


def test_parent_config_reload(ats_factory: ATSFactory) -> None:
    """parent.config reloads for file and record changes."""

    ParentConfigReloadScenario(ats_factory).run()
