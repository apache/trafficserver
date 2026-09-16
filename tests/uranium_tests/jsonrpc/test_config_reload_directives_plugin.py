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
"""Test TSCfgLoadCtxGetReloadDirectives through a custom plugin."""

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

from .config_reload_helpers import require_command, rpc, wait_for_status


class ConfigReloadDirectivesPluginScenario:
    """Separate reload directives from supplied plugin configuration."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the directives test plugin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Install the plugin and its file-based baseline config.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config|config.reload|cfg_plugin_directives_test",
            })
        ats.write_config_file("cfg_plugin_directives_test.conf", "initial: config\n")
        ats.copy_custom_plugin("plugins/.libs/cfg_plugin_directives_test.so")
        ats.plugin_config.add_line("cfg_plugin_directives_test.so cfg_plugin_directives_test.conf")
        return ats

    def reload(self, token: str, config: dict[str, object], *, force: bool = False) -> None:
        """Send one inline plugin reload and require it to be accepted.

        :param token: Tracking token for the reload.
        :param config: Supplied plugin YAML represented as a mapping.
        :param force: Whether to force the handler to run.
        """

        response = rpc(
            self._ats,
            "admin_config_reload",
            {
                "token": token,
                "configs": {
                    "cfg_plugin_directives_test": config
                },
                **({
                    "force": True
                } if force else {}),
            },
        )
        assert "error" not in response, response
        assert not response["result"].get("errors"), response

    def run(self) -> None:
        """Exercise inline directives, file mode, and empty directives."""

        self._ats.start()
        wait_for_file_lines(self._ats.traffic_out, "TSCfgRegister OK", 1)

        self.reload(
            "rpc-with-directives",
            {
                "greeting": "hello_directives",
                "_reload": {
                    "version": "2.0"
                }
            },
        )
        wait_for_status(
            self._ats,
            "rpc-with-directives",
            "directive_version=2.0",
            "content_greeting=hello_directives",
            "success",
            "[plugin: ",
        )

        config_file = self._ats.config_directory / "cfg_plugin_directives_test.conf"
        require_command(self._ats.run_shell(f'touch "{config_file}" && traffic_ctl config reload -t file-no-directives'))
        wait_for_status(self._ats, "file-no-directives", "no_directives", "file_mode", "success")

        self.reload("rpc-empty-directives", {"greeting": "empty_dir", "_reload": {}}, force=True)
        wait_for_status(
            self._ats,
            "rpc-empty-directives",
            "directive_version=none",
            "content_greeting=empty_dir",
            "success",
        )
        diagnostics = wait_for_file_lines(self._ats.diags_log, r"Config reload \[rpc-with-directives\]", 1)
        assert "Config reload [rpc-with-directives] completed" in diagnostics


def test_config_reload_directives_plugin(ats_factory: ATSFactory) -> None:
    """Plugin reload directives remain separate from supplied YAML.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadDirectivesPluginScenario(ats_factory).run()
