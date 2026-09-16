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
"""Test deferred TSCfgLoadCtx completion during a full reload."""

import time

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

from .config_reload_helpers import require_command, wait_for_status


class ConfigReloadDeferredScenario:
    """Exercise delayed plugin success and failure alongside core tasks."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the deferred reload plugin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Install the deferred plugin and its initial successful config.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config|config.reload|cfg_plugin_deferred_test",
            })
        ats.write_config_file("cfg_plugin_deferred_test.conf", "mode: success\n")
        ats.copy_custom_plugin("plugins/.libs/cfg_plugin_deferred_test.so")
        ats.plugin_config.add_line("cfg_plugin_deferred_test.so cfg_plugin_deferred_test.conf")
        return ats

    def trigger(self, token: str, *, fail: bool = False) -> None:
        """Touch all participating files and start one full reload.

        :param token: Tracking token for the new reload.
        :param fail: Whether to request the plugin's deferred failure path.
        """

        config = self._ats.config_directory
        update = f'printf "defer_fail: true\\n" > "{config / "cfg_plugin_deferred_test.conf"}" && ' if fail else ""
        command = (
            f'{update}touch "{config / "ip_allow.yaml"}" "{config / "sni.yaml"}" '
            f'"{config / "cfg_plugin_deferred_test.conf"}" && traffic_ctl config reload -t {token}')
        require_command(self._ats.run_shell(command))

    def run(self) -> None:
        """Verify in-progress state, eventual success, and eventual failure."""

        self._ats.start()
        wait_for_file_lines(self._ats.traffic_out, "TSCfgRegister OK", 1)

        self.trigger("full-deferred-ok")
        time.sleep(1)
        require_command(
            self._ats.traffic_ctl("config", "status", "-t", "full-deferred-ok"),
            "in-progress",
            "cfg_plugin_deferred_test",
            "deferring work",
        )
        wait_for_status(
            self._ats,
            "full-deferred-ok",
            "success",
            "deferred complete after heavy work",
            "stage 0",
            "stage 1",
            "ip_allow",
            "sni",
        )

        self.trigger("full-deferred-fail", fail=True)
        wait_for_status(
            self._ats,
            "full-deferred-fail",
            "fail",
            "deferred fail after heavy work",
            "heavy work failed",
            "ip_allow",
        )
        diagnostics = wait_for_file_lines(self._ats.diags_log, r"Config reload \[full-deferred-fail\]", 1)
        assert "Config reload [full-deferred-ok] completed" in diagnostics
        assert "Config reload [full-deferred-fail] finished with failures" in diagnostics


def test_config_reload_deferred(ats_factory: ATSFactory) -> None:
    """Deferred plugin tasks keep full reloads open and propagate failure.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadDeferredScenario(ats_factory).run()
