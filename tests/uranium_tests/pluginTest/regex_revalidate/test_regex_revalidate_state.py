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

import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, wait_for_file_lines


class RegexRevalidateStateScenario:
    """Merge a regex_revalidate state file into the startup configuration."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._path0_expiry = int(time.time()) + 90
        self._path1_epoch = int(time.time()) - 50
        self._path1_expiry = int(time.time()) + 600
        self._ats = self.configure_ats(ats_factory)
        self._state_path = self._ats.runtime_directory / "reval.state"

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the origin referenced by remap.config."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=300\r\n\r\n",
                "body": "xxx",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the plugin with both rules and the initial state."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("regex_revalidate.so"):
            pytest.skip("regex_revalidate.so is not installed")
        state_path = ats.runtime_directory / "reval.state"
        ats.plugin_config.add_line(f"regex_revalidate.so -d -c reval.conf -l reval.log -f {state_path}")
        ats.write_config_file(
            "reval.conf",
            f"path0 {self._path0_expiry} STALE\n"
            f"path1 {self._path1_expiry} MISS\n",
        )
        ats.write_runtime_file(
            "reval.state",
            f"path1 {self._path1_epoch} {self._path1_expiry} MISS\n"
            f"dummy {self._path1_epoch} {self._path1_expiry} MISS\n",
        )
        ats.remap_config.add_line(f"map http://ats/ http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "regex_revalidate",
                "proxy.config.http.wait_for_cache": 1,
            })
        return ats

    def check_merged_state(self) -> None:
        """Verify only configured rules survive and their epoch data merges."""

        content = wait_for_file_lines(self._state_path, r"^path0 ", 1)
        lines = content.splitlines()
        assert len(lines) == 2, content
        assert re.fullmatch(rf"path0 \d+ {self._path0_expiry} STALE", lines[0]), content
        assert lines[1] == f"path1 {self._path1_epoch} {self._path1_expiry} MISS"

    def run(self) -> None:
        """Start the services and validate the rewritten state file."""

        self._origin.start()
        self._ats.start()
        self.check_merged_state()


def test_regex_revalidate_state(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """regex_revalidate merges persisted epochs for matching startup rules."""

    RegexRevalidateStateScenario(ats_factory, services).run()
