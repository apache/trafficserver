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
"""Verify regex_revalidate rule additions and immutable rule expiry."""

import os
from pathlib import Path
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines, wait_for_metric


def test_regex_revalidate(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Rules stale matching objects once and retain their first expiry.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    def configure_server(services: ServiceFactory) -> OriginServer:
        """Create the three long-lived cacheable resources.

        :param services: Factory owning support services and their cleanup.
        """

        origin = services.origin("origin")
        for path, etag, max_age, body in (
            ("/path1", "path1", 600, "abc"),
            ("/path1a", "path1a", 600, "cde"),
            ("/path2a", "path2a", 900, "efg"),
        ):
            origin.add_response(
                {"headers": f"GET {path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
                {
                    "headers":
                        (
                            f'HTTP/1.1 200 OK\r\nConnection: close\r\nEtag: "{etag}"\r\n'
                            f"Cache-Control: max-age={max_age},public\r\n\r\n"),
                    "body": body,
                },
            )
        return origin

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable regex_revalidate and cache-state response headers.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ats")
        if not ats.plugin_exists("regex_revalidate.so") or not ats.plugin_exists("xdebug.so"):
            pytest.skip("regex_revalidate.so and xdebug.so are required")
        ats.write_config_file("regex_revalidate.conf", "# Empty\n")
        ats.plugin_config.add_lines(("xdebug.so --enable=x-cache", "regex_revalidate.so -d -c regex_revalidate.conf"))
        ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "regex_revalidate",
                "proxy.config.http.insert_age_in_response": 0,
                "proxy.config.http.response_via_str": 3,
            })
        return ats

    def rules_path() -> Path:
        """Return the active regex rule file."""

        return _ats.config_directory / "regex_revalidate.conf"

    def request(path: str, expected_cache: str) -> None:
        """Request a resource and verify its cache lookup state.

        :param path: Resource or file path used by this operation.
        :param expected_cache: Expected cache for this case.
        """

        result = _curl.get(
            _ats,
            path,
            headers={
                "x-debug": "x-cache",
                "Host": "www.example.com"
            },
            options=f"--silent --dump-header - --output /dev/null",
        )
        assert result.returncode == 0, result.output
        assert f"X-Cache: {expected_cache}" in result.stdout, result.output

    def reload_rules(*rules: str) -> None:
        """Write a new generation and wait until the plugin lists it.

        :param rules: Rules used by this test step.
        """
        nonlocal _loaded_count, _mtime

        _mtime = max(_mtime, int(rules_path().stat().st_mtime) + 2, int(time.time()) + 2)
        rules_path().write_text("\n".join(rules) + "\n")
        os.utime(rules_path(), (_mtime, _mtime))
        _mtime += 1
        result = _ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
        assert result.returncode == 0, result.output
        _loaded_count += len(rules)
        wait_for_file_lines(_ats.traffic_out, "result: STALE", _loaded_count, timeout=15)

    _origin = configure_server(services)
    _ats = configure_ats(ats_factory)
    _curl = Curl(ats_factory.run_directory)
    now = int(time.time())
    _path1_rule = f"path1 {now + 600}"
    _path2_rule = f"path2 {now + 700}"
    _mtime = now + 1
    _loaded_count = 0

    _origin.start()
    _ats.start()
    for path in ("/path1", "/path1a", "/path2a"):
        request(path, "miss")
    request("/path1", "hit-fresh")
    reload_rules(_path1_rule)
    request("/path1", "hit-stale")
    request("/path1", "hit-fresh")
    reload_rules(_path1_rule, _path2_rule)
    request("/path1", "hit-fresh")
    request("/path1a", "hit-stale")
    reload_rules(_path1_rule, f"path2 {int(time.time()) - 100}")
    request("/path2a", "hit-stale")
    wait_for_metric(_ats, "plugin.regex_revalidate.stale", 3, timeout=30)
    wait_for_metric(_ats, "plugin.regex_revalidate.miss", 0, timeout=30)
