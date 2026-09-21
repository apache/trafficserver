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
"""Test the TSCfg plugin configuration API end to end."""

import time

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

from .config_reload_helpers import require_command, rpc, wait_for_status


class ConfigReloadPluginApiScenario:
    """Exercise registration, supplied YAML, subtasks, and triggers."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the TSCfg API test plugin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Install the plugin with its main and companion files.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config|config.reload|cfg_plugin_test",
            })
        ats.write_config_file("cfg_plugin_test.conf", "greeting: hello\n")
        ats.write_config_file("cfg_plugin_companion.conf", "companion: data\n")
        ats.copy_custom_plugin("plugins/.libs/cfg_plugin_test.so")
        ats.plugin_config.add_line("cfg_plugin_test.so cfg_plugin_test.conf cfg_plugin_companion.conf")
        return ats

    def reload(self, token: str, config: dict[str, object], *, force: bool = False) -> None:
        """Dispatch supplied YAML to the test plugin.

        :param token: Tracking token for the reload.
        :param config: Plugin configuration supplied over JSON-RPC.
        :param force: Whether to force the registered handler to run.
        """

        response = rpc(
            self._ats,
            "admin_config_reload",
            {
                "token": token,
                "configs": {
                    "cfg_plugin_test": config
                },
                **({
                    "force": True
                } if force else {}),
            },
        )
        assert "error" not in response, response
        errors = response["result"].get("errors", [])
        if token == "rpc-greet":
            assert not errors, response
        assert "6011" not in str(errors) and "6010" not in str(errors), response

    def run(self) -> None:
        """Run every registration and handler API behavior."""

        self._ats.start()
        startup = wait_for_file_lines(self._ats.traffic_out, "TSCfgAddFileDependency OK", 1)
        assert "TSCfgRegister OK" in startup
        assert "TSCfgAttachReloadTrigger OK" in startup

        self.reload("rpc-greet", {"greet": "world"})
        wait_for_status(self._ats, "rpc-greet", "[plugin: ", "greet=world", "success", "handler entered")

        self.reload("rpc-fail", {"fail_on_purpose": True}, force=True)
        wait_for_status(self._ats, "rpc-fail", "[plugin: ", "fail", "fail requested")

        self.reload("rpc-subtask", {"with_subtask": True}, force=True)
        wait_for_status(
            self._ats,
            "rpc-subtask",
            "cfg_plugin_test",
            "cfg_plugin_test_subtask",
            "subtask done",
            "subtask log entry",
        )

        self.reload("rpc-subtask-fail", {"subtask_fail": True}, force=True)
        wait_for_status(
            self._ats,
            "rpc-subtask-fail",
            "cfg_plugin_test_failing_subtask",
            "subtask failed on purpose",
        )

        trigger = "RecordTriggeredReloadContinuation: executing reload for config 'cfg_plugin_test'"
        previous_triggers = self._ats.traffic_out.read_text(errors="replace").count(trigger)
        require_command(self._ats.traffic_ctl("config", "set", "proxy.config.http.insert_age_in_response", "0"))
        wait_for_file_lines(self._ats.traffic_out, trigger, previous_triggers + 1, timeout=20)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            status = self._ats.traffic_ctl("config", "status", "-c", "all")
            if status.returncode == 0 and "cfg_plugin_test" in status.output and "in_progress" not in status.output:
                break
            time.sleep(0.2)
        else:
            raise AssertionError(f"Record-triggered plugin reload did not finish:\n{status.output}")

        require_command(self._ats.traffic_ctl("config", "reload", "-t", "core-check", "-F"))
        wait_for_status(self._ats, "core-check", "success", excludes=("ip_allow [plugin]",))

        diagnostics = wait_for_file_lines(self._ats.diags_log, r"Config reload \[core-check\]", 1)
        for fragment in (
                "Config reload [rpc-greet] completed",
                "Config reload [rpc-fail] finished with failures",
                "Config reload [rpc-subtask] completed",
                "Config reload [rpc-subtask-fail] finished with failures",
                "Config reload [core-check] completed",
        ):
            assert fragment in diagnostics
        assert "ignoring transition from" not in diagnostics


def test_config_reload_plugin_api(ats_factory: ATSFactory) -> None:
    """All public TSCfg registration and load-context APIs cooperate.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadPluginApiScenario(ats_factory).run()
