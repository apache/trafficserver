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


class ConfigReloadFullSmokeScenario:
    """Reload every file handler and representative record handlers."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Create valid content for handlers that reject empty files."""

        ats = ats_factory.create("ts", enable_cache=True)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config|reload",
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
                "name": "smoke",
                "format": "%<cqtq>",
            }]
        }})
        ats.write_config_file(
            "sni.yaml",
            'sni:\n  - fqdn: "*.example.com"\n    verify_client: NONE\n',
        )
        return ats

    def touch_all_config_files(self) -> None:
        """Bump every registered file handler's mtime."""

        filenames = (
            "ip_allow.yaml",
            "parent.config",
            "cache.config",
            "hosting.config",
            "splitdns.config",
            "logging.yaml",
            "sni.yaml",
            "ssl_multicert.yaml",
        )
        for filename in filenames:
            path = self._ats.config_directory / filename
            path.touch(exist_ok=True)

    def file_reload(self) -> None:
        """Reload records and every file handler under one named token."""

        cold = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.diags.debug.tags",
            "rpc|config|reload|upd",
            "--cold",
        )
        assert cold.returncode == 0, cold.output
        self.touch_all_config_files()
        reload_result = self._ats.traffic_ctl("config", "reload", "-t", "full_reload_smoke")
        assert reload_result.returncode == 0, reload_result.output
        time.sleep(15)
        status = self._ats.traffic_ctl("config", "status", "-t", "full_reload_smoke")
        assert status.returncode == 0, status.output
        assert "in_progress" not in status.stdout

    def record_reloads(self) -> None:
        """Exercise one live trigger record from logging and SSL."""

        for name, value in (
            ("proxy.config.log.sampling_frequency", "2"),
            ("proxy.config.ssl.server.session_ticket.enable", "0"),
        ):
            time.sleep(2)
            result = self._ats.traffic_ctl("config", "set", name, value)
            assert result.returncode == 0, result.output
        time.sleep(10)
        history = self._ats.traffic_ctl("config", "status", "-c", "all")
        assert history.returncode == 0, history.output

    def run(self) -> None:
        """Run full file and record reload smoke coverage."""

        self._ats.start()
        time.sleep(3)
        self.file_reload()
        self.record_reloads()
        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "ignoring transition from" not in diagnostics


def test_config_reload_full_smoke(ats_factory: ATSFactory) -> None:
    """All registered reload paths finish without terminal-state conflicts."""

    ConfigReloadFullSmokeScenario(ats_factory).run()
