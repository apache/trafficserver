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


def test_ip_allow_reload_triggered(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ip_allow watches its own file, category file, and category record only.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    def configure_server(services: ServiceFactory) -> OriginServer:
        """Create the protected origin resource.

        :param services: Factory owning support services and their cleanup.
        """

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
                "body": "ok"
            },
        )
        return origin

    def configure_categories() -> tuple[Path, Path, Path, Path]:
        """Create allow, deny, restore, and active category documents."""

        allow = _run_directory / "categories_allow.yaml"
        deny = _run_directory / "categories_deny.yaml"
        restore = _run_directory / "categories_restore.yaml"
        active = _run_directory / "ip_categories.yaml"
        allow.write_text("ip_categories:\n  - name: INTERNAL\n    ip_addrs: 127.0.0.1\n")
        deny.write_text("ip_categories:\n  - name: INTERNAL\n    ip_addrs: 1.2.3.4\n")
        restore.write_text(allow.read_text())
        shutil.copyfile(allow, active)
        return allow, deny, restore, active

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Allow all INTERNAL traffic and only HEAD for other clients.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ats")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ip_allow|config",
                "proxy.config.cache.ip_categories.filename": str(_active_file),
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
        ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
        return ats

    def change_mtime(path: Path) -> None:
        """Advance a config file timestamp beyond one-second detection granularity.

        :param path: Resource or file path used by this operation.
        """
        nonlocal _mtime

        _mtime = max(_mtime, int(path.stat().st_mtime) + 2, int(time.time()) + 2)
        os.utime(path, (_mtime, _mtime))
        _mtime += 1

    def reload(*, expect_ip_allow: bool) -> None:
        """Run a full reload and verify whether ip_allow participated.

        :param expect_ip_allow: Expect ip allow used by this test step.
        """
        nonlocal _load_count

        result = _ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        if expect_ip_allow:
            _load_count += 1
            wait_for_file_lines(_ats.diags_log, "ip_allow.yaml finished loading", _load_count, timeout=15)
        else:
            time.sleep(2)
            content = _ats.diags_log.read_text(errors="replace")
            assert content.count("ip_allow.yaml finished loading") == _load_count

    def status() -> str:
        """Return the response status for a GET from the loopback client."""

        result = _curl.get(
            _ats,
            "/test",
            options=f"--silent --output /dev/null --write-out '%{{http_code}}'",
        )
        assert result.returncode == 0, result.output
        return result.stdout

    _run_directory = ats_factory.run_directory
    _origin = configure_server(services)
    _allow_file, _deny_file, _restore_file, _active_file = configure_categories()
    _ats = configure_ats(ats_factory)
    _curl = Curl(ats_factory.run_directory)
    _load_count = 1
    _mtime = int(time.time()) + 2

    _origin.start()
    _ats.start()
    change_mtime(_ats.config_directory / "ip_allow.yaml")
    reload(expect_ip_allow=True)
    change_mtime(_active_file)
    reload(expect_ip_allow=True)
    change_mtime(_ats.config_directory / "hosting.config")
    reload(expect_ip_allow=False)
    assert status() == "200"
    shutil.copyfile(_deny_file, _active_file)
    change_mtime(_active_file)
    reload(expect_ip_allow=True)
    assert status() == "403"

    result = _ats.traffic_ctl(
        "config",
        "set",
        "proxy.config.cache.ip_categories.filename",
        str(_restore_file),
    )
    assert result.returncode == 0, result.output
    _load_count += 1
    wait_for_file_lines(_ats.diags_log, "ip_allow.yaml finished loading", _load_count, timeout=30)
    assert status() == "200"
