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
"""Request-date-sensitive range-cache refresh checks."""
import email.utils
import time
import pytest
from tools.uranium.services import ATS, Curl, ServiceFactory


def test_cache_range_requests_ims(ats: ATS, services: ServiceFactory, curl: Curl) -> None:
    """Refresh cached ranges using both supported IMS header names.

    :param ats: Cache-enabled proxy.
    :param services: Origin factory.
    :param curl: ATS-aware request runner.
    """
    if not all(ats.plugin_exists(name) for name in ("cache_range_requests.so", "xdebug.so")):
        pytest.skip("cache_range_requests.so and xdebug.so are required")
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /path HTTP/1.1\r\nHost: www.example.com\r\nRange: bytes=0-\r\n\r\n"}, {
            "headers":
                'HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nCache-Control: max-age=500\r\n'
                'Content-Range: bytes 0-17/17\r\nConnection: close\r\nEtag: "772102f4-56f4bc1e6d417"\r\n\r\n',
            "body": "lets go surfin now"
        })
    ats.remap_config.add_lines(
        [
            f"map http://ims http://127.0.0.1:{origin.port} @plugin=cache_range_requests.so @pparam=--consider-ims",
            f"map http://imsheader http://127.0.0.1:{origin.port} @plugin=cache_range_requests.so "
            "@pparam=--consider-ims @pparam=--ims-header=CrrIms",
        ])
    ats.plugin_config.add_line("xdebug.so")
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "cache_range_requests"})
    origin.start()
    ats.start()
    for host, header, expected in (
        ("ims", "", "miss"),
        ("ims", "", "hit-fresh"),
        ("ims", f"-H 'X-CRR-IMS: {email.utils.formatdate(time.time() + 100, usegmt=True)}'", "hit-stale"),
        ("imsheader", f"-H 'CrrIms: {email.utils.formatdate(time.time() + 110, usegmt=True)}'", "hit-stale"),
    ):
        result = curl.run_for(
            ats, f"-s -D - -H 'x-debug: x-cache' -H 'Host: {host}' -r 0- "
            f"{header} http://127.0.0.1:{ats.http_port}/path")
        assert result.returncode == 0, result.output
        assert f"X-Cache: {expected}" in result.stdout.splitlines(), result.output
