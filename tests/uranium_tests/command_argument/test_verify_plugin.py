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

VERIFY_PLUGIN__PLUGINS = (
    "missing_ts_plugin_init.so",
    "conf_remap_stripped.so",
    "ssl_hook_test.so",
    "missing_mangled_definition.so",
)


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Create a runroot containing every plugin used by verification cases.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    for plugin in VERIFY_PLUGIN__PLUGINS:
        ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{plugin}")
    return ats


def verify(plugin: str | None, *, return_code: int, diagnostic: str, _ats: ATS, _command: str) -> CommandResult:
    """Run one verification command and check its result.

    :param _ats: Test-local ats configured by the test.
    :param _command: Test-local command configured by the test.
    :param plugin: Plugin used by this test step.
    :param return_code: Expected process exit status.
    :param diagnostic: Diagnostic used by this test step.
    """

    argument = _command
    if plugin is not None:
        path = Path(plugin) if plugin.startswith("/") else _ats.run_directory / "plugin" / plugin
        argument += f" {path}"
    result = _ats.run("traffic_server", "-C", argument)
    assert result.returncode == return_code, result.output
    assert re.search(diagnostic, result.stderr), result.output
    return result


def test_verify_global_plugin(ats_factory: ATSFactory) -> None:
    """The global-plugin verifier accepts only loadable global plugins.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    command = "verify_global_plugin"
    _ats = configure_ats(ats_factory)

    _ats.start()
    verify(None, return_code=1, diagnostic=r"requires a plugin SO file path argument", _ats=_ats, _command=command)
    verify("/this/file/does/not/exist.so", return_code=1, diagnostic=r"No such file or directory", _ats=_ats, _command=command)
    verify(
        "missing_ts_plugin_init.so", return_code=1, diagnostic=r"unable to find TSPluginInit function", _ats=_ats, _command=command)
    verify("conf_remap_stripped.so", return_code=1, diagnostic=r"unable to find TSPluginInit function", _ats=_ats, _command=command)
    verify("ssl_hook_test.so", return_code=0, diagnostic=r"verifying plugin .* Success", _ats=_ats, _command=command)
    verify("missing_mangled_definition.so", return_code=1, diagnostic=r"unable to load", _ats=_ats, _command=command)


def test_verify_remap_plugin(ats_factory: ATSFactory) -> None:
    """The remap-plugin verifier accepts only plugins with the remap API.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    command = "verify_remap_plugin"
    _ats = configure_ats(ats_factory)

    _ats.start()
    verify(None, return_code=1, diagnostic=r"requires a plugin SO file path argument", _ats=_ats, _command=command)
    verify("/this/file/does/not/exist.so", return_code=1, diagnostic=r"No such file or directory", _ats=_ats, _command=command)
    verify(
        "missing_ts_plugin_init.so",
        return_code=1,
        diagnostic=r"missing required function TSRemapInit",
        _ats=_ats,
        _command=command)
    verify("ssl_hook_test.so", return_code=1, diagnostic=r"missing required function TSRemapInit", _ats=_ats, _command=command)
    verify("conf_remap_stripped.so", return_code=0, diagnostic=r"verifying plugin .* Success", _ats=_ats, _command=command)
