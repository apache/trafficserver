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
"""Test SSL coordinator reload state propagation and severity tags."""

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

from .config_reload_helpers import require_command, wait_for_status


class ConfigReloadSslStateScenario:
    """Exercise SSL success, partial failure, filtering, and recovery."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure a TLS-enabled Traffic Server.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Create valid ssl_multicert and SNI baseline files.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "config.reload",
            })
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.write_config_file("sni.yaml", 'sni:\n- fqdn: "*.example.com"\n  verify_client: NONE\n')
        return ats

    def reload(self, token: str) -> None:
        """Touch the SSL inputs and start a tracked reload.

        :param token: Reload tracking token.
        """

        config = self._ats.config_directory
        require_command(
            self._ats.run_shell(
                f'touch "{config / "sni.yaml"}" "{config / "ssl_multicert.yaml"}" '
                f'&& traffic_ctl config reload -t {token}'))

    def run(self) -> None:
        """Verify failure propagation, filtering, and successful recovery."""

        self._ats.start()
        self.reload("ssl-baseline")
        wait_for_status(
            self._ats,
            "ssl-baseline",
            "[Note]",
            "ssl_client_coordinator",
            "SSLConfig loading",
            "SSLConfig reloaded",
            excludes=("in_progress", "FAIL"),
        )

        config = self._ats.config_directory
        broken_sni = (
            "sni:\n- fqdn: example.com\n  client_cert: /nonexistent/bad.pem\n"
            "  client_key: /nonexistent/bad.key\n  verify_client: STRICT\n")
        require_command(
            self._ats.run_shell(
                f'printf %b {broken_sni!r} > "{config / "sni.yaml"}" '
                f'&& touch "{config / "ssl_multicert.yaml"}" '
                "&& traffic_ctl config reload -t ssl-sni-fail"))
        wait_for_status(
            self._ats,
            "ssl-sni-fail",
            "FAIL",
            "[Err]",
            "SSLConfig reloaded",
            "sni.yaml failed to load",
        )
        require_command(
            self._ats.traffic_ctl("config", "status", "-t", "ssl-sni-fail", "--min-level", "warning"),
            "[Err]",
            excludes=("[Note]",),
        )

        valid_sni = 'sni:\n- fqdn: "*.example.com"\n  verify_client: NONE\n'
        require_command(
            self._ats.run_shell(
                f'printf %b {valid_sni!r} > "{config / "sni.yaml"}" '
                f'&& touch "{config / "ssl_multicert.yaml"}" '
                "&& traffic_ctl config reload -t ssl-recovery"))
        wait_for_status(self._ats, "ssl-recovery", "[Note]", excludes=("FAIL", "in_progress"))

        diagnostics = wait_for_file_lines(self._ats.diags_log, r"Config reload \[ssl-recovery\]", 1)
        assert "Config reload [ssl-baseline] completed" in diagnostics
        assert "Config reload [ssl-sni-fail] finished with failures" in diagnostics
        assert "Config reload [ssl-recovery] completed" in diagnostics
        assert "ignoring transition from" not in diagnostics


def test_config_reload_ssl_state(ats_factory: ATSFactory) -> None:
    """SSL child state and severity propagate through the coordinator.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadSslStateScenario(ats_factory).run()
