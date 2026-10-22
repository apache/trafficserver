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
"""Exercise the original simple prefetch sequence."""
from pathlib import Path

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold, wait_for_file_lines


def test_prefetch_simple(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Fetching demo-1 also fetches its next three numbered resources.

    :param ats_factory: Factory for the caching proxy.
    :param services: Factory for the origin and DNS.
    :param curl: Transport-aware request runner.
    """
    origin = services.origin("origin")
    for number in range(1, 5):
        origin.add_response(
            {"headers": f"GET /texts/demo-{number}.txt HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"}, {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=85000\r\n\r\n",
                "body": f"This is the body for demo-{number}.txt.\n"
            })
    dns = services.dns("dns", default="127.0.0.1")
    ats = ats_factory.create("ts")
    if not all(ats.plugin_exists(plugin) for plugin in ("cachekey.so", "prefetch.so")):
        pytest.skip("cachekey.so and prefetch.so are required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|prefetch",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.remap_config.add_line(
        f"map http://domain.in http://127.0.0.1:{origin.port} "
        "@plugin=cachekey.so @pparam=--remove-all-params=true "
        "@plugin=prefetch.so @pparam=--front=true @pparam=--fetch-policy=simple "
        r"@pparam=--fetch-path-pattern=/(.*-)(\d+)(.*)/$1{$2+1}$3/ @pparam=--fetch-count=3")
    origin.start()
    dns.start()
    ats.start()
    result = curl.run_for(ats, f"--verbose --proxy 127.0.0.1:{ats.http_port} http://domain.in/texts/demo-1.txt")
    assert result.returncode == 0, result.output
    content = wait_for_file_lines(ats.traffic_out, r"^GET http://domain.in/texts/demo-", 4)
    requests = "\n".join(line for line in content.splitlines() if "GET http://domain.in" in line) + "\n"
    assert_matches_gold(requests, Path(__file__).parent / "prefetch_simple.gold")
