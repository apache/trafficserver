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


class ConfigReloadReserveSubtaskScenario:
    """Exercise subtask reservation after records.yaml completes first."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure every reload handler involved in the race."""

        ats = ats_factory.create("ts", enable_cache=True)
        ats.set_startup_timeout(30)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config|config.reload|filemanager",
            })
        ats.write_config_file(
            "ip_allow.yaml",
            "ip_allow:\n"
            "  - apply: in\n"
            "    ip_addrs: 0/0\n"
            "    action: allow\n"
            "    methods: ALL\n",
        )
        ats.set_logging_yaml({"logging": {
            "formats": [{
                "name": "reserve_test",
                "format": "%<cqtq>",
            }]
        }})
        ats.write_config_file(
            "sni.yaml",
            'sni:\n  - fqdn: "*.example.com"\n    verify_client: NONE\n',
        )
        return ats

    def change_records_without_notifying_ats(self) -> None:
        """Change a trigger record on disk before the explicit reload."""

        result = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.diags.debug.tags",
            "rpc|config|config.reload|filemanager|upd",
            "--cold",
        )
        assert result.returncode == 0, result.output

    def touch_other_reload_files(self) -> None:
        """Make file-based handlers participate in the same reload."""

        paths = (
            self._ats.config_directory / "ip_allow.yaml",
            self._ats.config_directory / "logging.yaml",
            self._ats.config_directory / "sni.yaml",
            self._ats.config_directory / "cache.config",
        )
        for path in paths:
            path.touch()

    def reload_and_check_status(self) -> None:
        """Trigger a named reload and verify its terminal state."""

        result = self._ats.traffic_ctl("config", "reload", "-t", "reserve_subtask_test")
        assert result.returncode == 0, result.output
        time.sleep(15)
        status = self._ats.traffic_ctl("config", "status", "-t", "reserve_subtask_test")
        assert status.returncode == 0, status.output
        assert "success" in status.stdout
        assert "in_progress" not in status.stdout

    def check_reload_diagnostics(self) -> None:
        """Verify reservation succeeded without conflicting transitions."""

        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "Reserved subtask" in traffic_out
        assert "ignoring transition from" not in traffic_out

    def run(self) -> None:
        """Run the complete reload race scenario."""

        self._ats.start()
        time.sleep(3)
        self.change_records_without_notifying_ats()
        self.touch_other_reload_files()
        self.reload_and_check_status()
        self.check_reload_diagnostics()


def test_config_reload_reserve_subtask(ats_factory: ATSFactory) -> None:
    """Reload handlers can reserve children after the parent first succeeds."""

    ConfigReloadReserveSubtaskScenario(ats_factory).run()
