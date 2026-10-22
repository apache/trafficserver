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
"""Verify complete large-body delivery with HTTP/2 stream priority enabled."""

import hashlib
from pathlib import Path
import re

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold


def test_http2_priority(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Preserve the original complete-response SHA256 and HTTP/2 header checks.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning the microserver origin.
    :param curl: Transport-aware curl runner with HTTP/2 support.
    """

    if not curl.supports("HTTP2"):
        pytest.skip("curl requires HTTP/2 support")
    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET /bigfile HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                    "Cache-Control: max-age=3600\r\nContent-Length: 1048576\r\n\r\n"),
            "body": "",
        },
    )
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    ats.records.update(
        {
            "proxy.config.http2.stream_priority_enabled": 1,
            "proxy.config.http2.no_activity_timeout_in": 3,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2",
        })
    origin.start()
    ats.start()
    body = ats.run_directory / "priority-body"
    result = curl.run_for(
        ats,
        f"--verbose --silent --show-error --insecure --http2 --output '{body}' "
        f"https://127.0.0.1:{ats.https_port}/bigfile",
        timeout=5,
    )
    assert result.returncode == 0, result.output
    assert re.search(r"< HTTP/2(?:\.0)? 200", result.stderr, re.IGNORECASE), result.stderr
    assert re.search(r"< server:\s*ATS/", result.stderr, re.IGNORECASE), result.stderr
    assert re.search(r"< content-length:\s*1048576", result.stderr, re.IGNORECASE), result.stderr
    digest = hashlib.sha256(body.read_bytes()).hexdigest()
    assert_matches_gold(f"{digest}  -\n", Path(__file__).parent / "gold/priority_0_stdout.gold")
