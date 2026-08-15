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
from pathlib import Path

from tools.uranium.services import ATS


class CacheConfigReloadScenario:
    """Reload cache.config and hosting.config after each file changes."""

    def __init__(self, ats: ATS) -> None:
        self.ats = ats

    def _configure_traffic_server(self) -> None:
        self.ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config",
        })
        self.ats.cache_config.add_line("dest_domain=example.com ttl-in-cache=30d")

    def _start_traffic_server(self) -> None:
        self.ats.start()

    def _reload_configuration(self, config_file: Path, token: str) -> None:
        config_file.touch()
        time.sleep(2)
        result = self.ats.traffic_ctl("config", "reload", "-m", "-t", token, "-w", "1", "-r", "0.5", "-T", "30s")
        assert result.returncode in (0, 2), result.output
        time.sleep(3)

    def _reload_cache_configuration(self) -> None:
        self._reload_configuration(self.ats.cache_config.path, "reload_cache_test")

    def _reload_hosting_configuration(self) -> None:
        self._reload_configuration(self.ats.hosting_config.path, "reload_hosting_test")

    def run(self) -> None:
        self._configure_traffic_server()
        self._start_traffic_server()
        self._reload_cache_configuration()
        self._reload_hosting_configuration()


def test_cache_config_reload(ats: ATS) -> None:
    CacheConfigReloadScenario(ats).run()
