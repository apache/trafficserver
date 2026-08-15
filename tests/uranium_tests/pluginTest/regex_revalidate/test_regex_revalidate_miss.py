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
"""Verify regex_revalidate MISS rules and rule-type transitions."""

from pathlib import Path
import os
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines, wait_for_metric


class RegexRevalidateMissScenario:
    """Drive cache state across MISS and STALE rule reloads."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._origin = self.configure_server()
        self._ats = self.configure_ats()
        self._curl = Curl(ats_factory.run_directory)
        self._rule = f"path1 {int(time.time()) + 600}"
        self._mtime = int(time.time()) + 1
        self._reload_counts = {"MISS": 0, "STALE": 0}

    def configure_server(self) -> OriginServer:
        """Create a cacheable origin resource."""

        origin = self._services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=300\r\n\r\n",
                "body": "xxx",
            },
        )
        origin.add_response(
            {"headers": "GET /path1 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers":
                    ('HTTP/1.1 200 OK\r\nConnection: close\r\nEtag: "path1"\r\n'
                     "Cache-Control: max-age=600,public\r\n\r\n"),
                "body": "abc",
            },
        )
        return origin

    def configure_ats(self) -> ATS:
        """Enable xdebug and regex_revalidate with an initially empty rule file."""

        ats = self._ats_factory.create("ats")
        if not ats.plugin_exists("regex_revalidate.so") or not ats.plugin_exists("xdebug.so"):
            pytest.skip("regex_revalidate.so and xdebug.so are required")
        ats.write_config_file("regex_revalidate.conf", "# Empty\n")
        ats.plugin_config.add_lines(
            (
                "xdebug.so --enable=x-cache",
                "regex_revalidate.so -d -c regex_revalidate.conf -l revalidate.log -m reval",
            ))
        ats.remap_config.add_line(f"map http://ats/ http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "regex_revalidate",
                "proxy.config.http.insert_age_in_response": 0,
                "proxy.config.http.response_via_str": 3,
                "proxy.config.http.cache.http": 1,
                "proxy.config.http.wait_for_cache": 1,
            })
        return ats

    @property
    def rules_path(self) -> Path:
        """Return the live plugin rule path."""

        return self._ats.config_directory / "regex_revalidate.conf"

    def request(self, expected_cache: str) -> None:
        """Request the cached resource and verify its x-cache state."""

        result = self._curl.run_for(
            self._ats,
            "--silent",
            "--dump-header",
            "-",
            "--output",
            "/dev/null",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
            "--header",
            "x-debug: x-cache",
            "http://ats/path1",
        )
        assert result.returncode == 0, result.output
        assert f"X-Cache: {expected_cache}" in result.stdout, result.output

    def reload_rule(self, mode: str) -> None:
        """Replace the active rule and wait for a full configuration reload."""

        self._mtime = max(self._mtime, int(self.rules_path.stat().st_mtime) + 2, int(time.time()) + 2)
        self.rules_path.write_text(f"{self._rule} {mode}\n")
        os.utime(self.rules_path, (self._mtime, self._mtime))
        self._mtime += 1
        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        self._reload_counts[mode] += 1
        wait_for_file_lines(
            self._ats.traffic_out,
            f"result: {mode}",
            self._reload_counts[mode],
            timeout=15,
        )

    def reload_unchanged(self) -> None:
        """Reload the unchanged rule file without resetting plugin state."""

        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Exercise rule additions, type changes, and unchanged reloads."""

        self._origin.start()
        self._ats.start()
        self.request("miss")
        self.request("hit-fresh")
        self.reload_rule("MISS")
        self.request("miss")
        self.request("hit-fresh")
        self.reload_rule("STALE")
        self.request("hit-stale")
        self.reload_rule("MISS")
        self.request("miss")
        self.request("hit-fresh")
        self.reload_unchanged()
        self.request("hit-fresh")
        self.rules_path.touch()
        os.utime(self.rules_path, (self._mtime, self._mtime))
        self._mtime += 1
        self.reload_unchanged()
        self.request("hit-fresh")
        wait_for_metric(self._ats, "plugin.regex_revalidate.stale", 1, timeout=30)
        wait_for_metric(self._ats, "plugin.regex_revalidate.miss", 2, timeout=30)


def test_regex_revalidate_miss(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """MISS rules refetch and reset correctly when changed to or from STALE."""

    RegexRevalidateMissScenario(ats_factory, services).run()
