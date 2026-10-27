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
"""Preserve the raw request matrix for X-Cache-Info and X-Effective-URL."""
import sys
from pathlib import Path

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory, assert_matches_gold


@pytest.mark.parametrize("scenario", ("x_cache_info", "x_effective_url"))
def test_xdebug_headers(ats_factory: ATSFactory, services: ServiceFactory, scenario: str) -> None:
    """Compare all four raw requests with the original diagnostic-header gold.

    :param ats_factory: Factory for the proxy.
    :param services: Factory for the origin and raw client.
    :param scenario: Header scenario directory.
    """
    directory = Path(__file__).parent / scenario
    origin = services.origin("origin")
    for path in ("/argh", "/two/argh"):
        origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"})
    ats = ats_factory.create("ts")
    if not ats.plugin_exists("xdebug.so"):
        pytest.skip("xdebug.so is required")
    ats.records.update({"proxy.config.url_remap.remap_required": 0})
    ats.plugin_config.add_line("xdebug.so")
    ats.remap_config.add_line(f"map http://one http://127.0.0.1:{origin.port}")
    suffix = "/two" if scenario == "x_effective_url" else ""
    ats.remap_config.add_line(f"map http://two http://127.0.0.1:{origin.port}{suffix}")
    ats.remap_config.add_line(f"regex_map http://three[0-9]+ http://127.0.0.1:{origin.port}")
    origin.start()
    ats.start()
    output = ""
    for name in ("none", "one", "two", "three"):
        result = ats.run(
            sys.executable,
            Path(__file__).parents[3] / "tools/tcp_client.py", "127.0.0.1", str(ats.http_port), directory / f"{name}.in")
        assert result.returncode == 0, result.output
        output += result.output + "======\n"
    output = output.replace(f":{origin.port}", ":SERVER_PORT")
    output = output.replace(str(ats.run_directory), f"/{scenario}/ts")
    assert_matches_gold(output, directory / "out.gold")
