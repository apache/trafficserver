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
"""Verify traffic_ctl parsing for repeated reload directives and data."""

from dataclasses import dataclass

from tools.uranium.services import ATS, ATSFactory


@dataclass(frozen=True)
class CommandCase:
    """One traffic_ctl invocation and its output contract."""

    arguments: tuple[str, ...]
    contains: tuple[str, ...]
    excludes: tuple[str, ...] = ()
    return_code: int = 0


class ConfigReloadDirectiveCliScenario:
    """Exercise the variable-arity ``-D`` and ``-d`` options."""

    _cases = (
        CommandCase(
            ("config", "reload", "-D", "ip_allow.id=foo", "-t", "cli_token_1", "-f", "rpc"),
            ('"token": "cli_token_1"', '"id": "foo"'),
            return_code=2),
        CommandCase(
            ("config", "reload", "-D", "ip_allow.id=1", "sni.id=2", "-t", "cli_token_2", "-f", "rpc"),
            ('"token": "cli_token_2"', '"ip_allow"', '"sni"'),
            return_code=2),
        CommandCase(
            ("config", "reload", "-D", "ip_allow.id=foo", "-d", "ip_allow: {rules: [x]}", "-f", "rpc"), ('"rules"', '"_reload"'),
            return_code=2),
        CommandCase(
            ("config", "reload", "--directive=ip_allow.id=foo", "-f", "rpc"), ('"id": "foo"',), ("Invalid directive format",),
            return_code=2),
        CommandCase(("config", "reload", "-D", "--", "-m"), ("Invalid directive format '-m'",), return_code=2),
        CommandCase(("config", "reload", "-D"), ("requires at least one",), return_code=2),
        CommandCase(("config", "reload", "-d"), ("requires content",), return_code=2),
        CommandCase(
            ("config", "reload", "-D", "ip_allow.id=1", "-D", "sni.id=2", "-f", "rpc"), ('"ip_allow"', '"sni"'), return_code=2),
        CommandCase(
            ("config", "reload", "-D", "ip_allow.id=1", "-t", "cli_token_8", "-D", "sni.id=2", "-f", "rpc"),
            ('"token": "cli_token_8"', '"ip_allow"', '"sni"'),
            return_code=2),
        CommandCase(
            ("config", "reload", "-d", "ip_allow: {rules: [x]}", "-d", "sni: {rules: [y]}", "-f", "rpc"), ('"ip_allow"', '"sni"'),
            return_code=2),
        CommandCase(("config", "reload", "-d", ""), ("received an empty value",), return_code=2),
        CommandCase(("config", "reload", "-D", ""), ("received an empty value",), return_code=2),
        CommandCase(("config", "reload", "-d", "", "-d", "ip_allow: {rules: [x]}"), ("received an empty value",), return_code=2),
    )

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the Traffic Server used for every CLI case.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable config-reload diagnostics and a valid ip_allow file.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config.reload",
        })
        ats.ip_allow_config.add_lines((
            "ip_allow:",
            "- apply: in",
            "  ip_addrs: 0/0",
            "  action: allow",
            "  methods: ALL",
        ))
        return ats

    def run(self) -> None:
        """Run each argument layout and validate its exit and output."""

        self._ats.start()
        for case in self._cases:
            result = self._ats.traffic_ctl(*case.arguments)
            assert result.returncode == case.return_code, result.output
            for expression in case.contains:
                assert expression in result.output, result.output
            for expression in case.excludes:
                assert expression not in result.output, result.output


def test_config_reload_directive_cli(ats_factory: ATSFactory) -> None:
    """Reload directives and data stop at later options and accumulate.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadDirectiveCliScenario(ats_factory).run()
