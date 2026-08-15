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

from tools.uranium.services import ATS, ATSFactory


class RecordsRunrootPrecedenceScenario:
    """Verify environment and runroot path records override records.yaml."""

    _PATH_RECORDS = (
        "proxy.config.bin_path",
        "proxy.config.local_state_dir",
        "proxy.config.log.logfile_dir",
        "proxy.config.plugin.plugin_dir",
    )

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure deliberately wrong path values and one environment override."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.bin_path": "wrong_bin_path",
                "proxy.config.local_state_dir": "wrong_runtime",
                "proxy.config.log.logfile_dir": "wrong_log",
                "proxy.config.plugin.plugin_dir": "wrong_plugin",
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "config_value",
            })
        ats.set_environment("PROXY_CONFIG_DIAGS_DEBUG_TAGS", "env_wins")
        ats.unset_environment(
            "PROXY_CONFIG_BIN_PATH",
            "PROXY_CONFIG_LOCAL_STATE_DIR",
            "PROXY_CONFIG_LOG_LOGFILE_DIR",
            "PROXY_CONFIG_PLUGIN_PLUGIN_DIR",
        )
        return ats

    def verify_startup_diagnostics(self) -> None:
        """Verify startup completed and reported each precedence override."""

        output = self._ats.traffic_out.read_text(errors="replace")
        assert "basic_string" not in output
        assert "records parsing completed" in output
        for record in self._PATH_RECORDS:
            assert f"'{record}' overridden with" in output
            assert "by runroot" in output
        assert "'proxy.config.diags.debug.tags' overridden with 'env_wins' by environment variable" in output

    def verify_runtime_values(self) -> None:
        """Verify runroot beat records.yaml and the environment beat both."""

        result = self._ats.traffic_ctl("config", "get", *self._PATH_RECORDS)
        assert result.returncode == 0, result.output
        for wrong_value in ("wrong_bin_path", "wrong_runtime", "wrong_log", "wrong_plugin"):
            assert wrong_value not in result.stdout

        result = self._ats.traffic_ctl("config", "get", "proxy.config.diags.debug.tags")
        assert result.returncode == 0, result.output
        assert "proxy.config.diags.debug.tags: env_wins" in result.stdout

    def run(self) -> None:
        """Start ATS with runroot active and validate record precedence."""

        self._ats.start()
        self.verify_startup_diagnostics()
        self.verify_runtime_values()


def test_records_runroot_precedence(ats_factory: ATSFactory) -> None:
    """Environment variables take precedence over runroot and records.yaml."""

    RecordsRunrootPrecedenceScenario(ats_factory).run()
