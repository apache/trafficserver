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


class ConfigReloadDedupScenario:
    """Trigger one reload handler through both record and file dependencies."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure valid files that participate in the reload."""

        ats = ats_factory.create("ts", enable_cache=True)
        ats.set_startup_timeout(30)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|config|config.reload|configproc",
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
                "name": "dedup_test",
                "format": "%<cqtq>",
            }]
        }})
        ats.write_config_file(
            "sni.yaml",
            'sni:\n  - fqdn: "*.example.com"\n    verify_client: NONE\n',
        )
        return ats

    def change_trigger_record(self) -> None:
        """Change the session-ticket record on disk without notifying ATS."""

        result = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.ssl.server.session_ticket.enable",
            "0",
            "--cold",
        )
        assert result.returncode == 0, result.output

    def touch_dependency_files(self) -> None:
        """Trigger the same SSL coordinator through sni.yaml's mtime."""

        for filename in ("ip_allow.yaml", "logging.yaml", "sni.yaml"):
            (self._ats.config_directory / filename).touch()

    def check_reload(self) -> None:
        """Wait for timer callbacks and verify the task remains consistent."""

        reload_result = self._ats.traffic_ctl("config", "reload", "-t", "dedup_test")
        assert reload_result.returncode == 0, reload_result.output
        time.sleep(15)
        status = self._ats.traffic_ctl("config", "status", "-t", "dedup_test")
        assert status.returncode == 0, status.output
        assert "in_progress" not in status.stdout
        assert "ssl_client_coordinator" in status.stdout
        assert "success" in status.stdout
        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "Reserved subtask" in traffic_out
        assert "ignoring transition from" not in traffic_out

    def run(self) -> None:
        """Run the record/file fan-in reload scenario."""

        self._ats.start()
        time.sleep(3)
        self.change_trigger_record()
        self.touch_dependency_files()
        self.check_reload()


def test_config_reload_dedup(ats_factory: ATSFactory) -> None:
    """Duplicate record and file triggers settle one reload task safely."""

    ConfigReloadDedupScenario(ats_factory).run()
