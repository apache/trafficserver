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
import re

from tools.uranium.services import ATS, ATSFactory, CommandResult


class VerifyPluginScenario:
    """Exercise traffic_server's global and remap plugin verification commands."""

    _PLUGINS = (
        "missing_ts_plugin_init.so",
        "conf_remap_stripped.so",
        "ssl_hook_test.so",
        "missing_mangled_definition.so",
    )

    def __init__(self, ats_factory: ATSFactory, command: str) -> None:
        self._command = command
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Create a runroot containing every plugin used by verification cases."""

        ats = ats_factory.create("ts")
        for plugin in self._PLUGINS:
            ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{plugin}")
        return ats

    def verify(
        self,
        plugin: str | None,
        *,
        return_code: int,
        diagnostic: str,
    ) -> CommandResult:
        """Run one verification command and check its result."""

        argument = self._command
        if plugin is not None:
            path = Path(plugin) if plugin.startswith("/") else self._ats.run_directory / "plugin" / plugin
            argument += f" {path}"
        result = self._ats.run("traffic_server", "-C", argument)
        assert result.returncode == return_code, result.output
        assert re.search(diagnostic, result.stderr), result.output
        return result

    def run_global(self) -> None:
        """Verify argument, symbol, load, and success behavior for global plugins."""

        self._ats.start()
        self.verify(None, return_code=1, diagnostic=r"requires a plugin SO file path argument")
        self.verify("/this/file/does/not/exist.so", return_code=1, diagnostic=r"No such file or directory")
        self.verify("missing_ts_plugin_init.so", return_code=1, diagnostic=r"unable to find TSPluginInit function")
        self.verify("conf_remap_stripped.so", return_code=1, diagnostic=r"unable to find TSPluginInit function")
        self.verify("ssl_hook_test.so", return_code=0, diagnostic=r"verifying plugin .* Success")
        self.verify("missing_mangled_definition.so", return_code=1, diagnostic=r"unable to load")

    def run_remap(self) -> None:
        """Verify argument, symbol, and success behavior for remap plugins."""

        self._ats.start()
        self.verify(None, return_code=1, diagnostic=r"requires a plugin SO file path argument")
        self.verify("/this/file/does/not/exist.so", return_code=1, diagnostic=r"No such file or directory")
        self.verify("missing_ts_plugin_init.so", return_code=1, diagnostic=r"missing required function TSRemapInit")
        self.verify("ssl_hook_test.so", return_code=1, diagnostic=r"missing required function TSRemapInit")
        self.verify("conf_remap_stripped.so", return_code=0, diagnostic=r"verifying plugin .* Success")


def test_verify_global_plugin(ats_factory: ATSFactory) -> None:
    """The global-plugin verifier accepts only loadable global plugins."""

    VerifyPluginScenario(ats_factory, "verify_global_plugin").run_global()


def test_verify_remap_plugin(ats_factory: ATSFactory) -> None:
    """The remap-plugin verifier accepts only plugins with the remap API."""

    VerifyPluginScenario(ats_factory, "verify_remap_plugin").run_remap()
