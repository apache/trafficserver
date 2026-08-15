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
"""Verify ip_allow dependencies trigger only the intended reloads."""

import os
from pathlib import Path
import shutil
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


class IpAllowReloadScenario:
    """Exercise file and record dependencies registered by ip_allow."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._run_directory = ats_factory.run_directory
        self._origin = self.configure_server(services)
        self._allow_file, self._deny_file, self._restore_file, self._active_file = self.configure_categories()
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)
        self._load_count = 1
        self._mtime = int(time.time()) + 2

    @staticmethod
    def configure_server(services: ServiceFactory) -> OriginServer:
        """Create the protected origin resource."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
                "body": "ok"
            },
        )
        return origin

    def configure_categories(self) -> tuple[Path, Path, Path, Path]:
        """Create allow, deny, restore, and active category documents."""

        allow = self._run_directory / "categories_allow.yaml"
        deny = self._run_directory / "categories_deny.yaml"
        restore = self._run_directory / "categories_restore.yaml"
        active = self._run_directory / "ip_categories.yaml"
        allow.write_text("ip_categories:\n  - name: INTERNAL\n    ip_addrs: 127.0.0.1\n")
        deny.write_text("ip_categories:\n  - name: INTERNAL\n    ip_addrs: 1.2.3.4\n")
        restore.write_text(allow.read_text())
        shutil.copyfile(allow, active)
        return allow, deny, restore, active

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow all INTERNAL traffic and only HEAD for other clients."""

        ats = ats_factory.create("ats")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ip_allow|config",
                "proxy.config.cache.ip_categories.filename": str(self._active_file),
            })
        ats.ip_allow_config.add_lines(
            """ip_allow:
  - apply: in
    ip_categories: INTERNAL
    action: allow
    methods: ALL
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods:
      - HEAD
""")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def change_mtime(self, path: Path) -> None:
        """Advance a config file timestamp beyond one-second detection granularity."""

        self._mtime = max(self._mtime, int(path.stat().st_mtime) + 2, int(time.time()) + 2)
        os.utime(path, (self._mtime, self._mtime))
        self._mtime += 1

    def reload(self, *, expect_ip_allow: bool) -> None:
        """Run a full reload and verify whether ip_allow participated."""

        result = self._ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        if expect_ip_allow:
            self._load_count += 1
            wait_for_file_lines(self._ats.diags_log, "ip_allow.yaml finished loading", self._load_count, timeout=15)
        else:
            time.sleep(2)
            content = self._ats.diags_log.read_text(errors="replace")
            assert content.count("ip_allow.yaml finished loading") == self._load_count

    def status(self) -> str:
        """Return the response status for a GET from the loopback client."""

        result = self._curl.get(
            self._ats,
            "/test",
            options=("--silent", "--output", "/dev/null", "--write-out", "%{http_code}"),
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def change_record(self) -> None:
        """Point the category record at the restore file and await its callback."""

        result = self._ats.traffic_ctl(
            "config",
            "set",
            "proxy.config.cache.ip_categories.filename",
            str(self._restore_file),
        )
        assert result.returncode == 0, result.output
        self._load_count += 1
        wait_for_file_lines(self._ats.diags_log, "ip_allow.yaml finished loading", self._load_count, timeout=30)

    def run(self) -> None:
        """Verify direct, dependent, unrelated, content, and record reload triggers."""

        self._origin.start()
        self._ats.start()
        self.change_mtime(self._ats.config_directory / "ip_allow.yaml")
        self.reload(expect_ip_allow=True)
        self.change_mtime(self._active_file)
        self.reload(expect_ip_allow=True)
        self.change_mtime(self._ats.config_directory / "hosting.config")
        self.reload(expect_ip_allow=False)
        assert self.status() == "200"
        shutil.copyfile(self._deny_file, self._active_file)
        self.change_mtime(self._active_file)
        self.reload(expect_ip_allow=True)
        assert self.status() == "403"
        self.change_record()
        assert self.status() == "200"


def test_ip_allow_reload_triggered(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ip_allow watches its own file, category file, and category record only."""

    IpAllowReloadScenario(ats_factory, services).run()
