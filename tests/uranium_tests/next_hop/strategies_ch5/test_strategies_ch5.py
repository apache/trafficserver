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

from tools.uranium.services import DNSServer
from pathlib import Path
import re

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
NUM_OBJECTS = 32
NUM_RINGS = 5
HOSTS_PER_RING = 2
NUM_HOSTS = NUM_RINGS * HOSTS_PER_RING


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the object set returned by the final ring.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    response = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
        "body": "This is the body.\n",
    }
    for index in range(NUM_OBJECTS):
        origin.add_response(
            {"headers": f"GET /obj{index} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
            response,
        )
    return origin


def configure_next_hop(ats_factory: ATSFactory, index: int, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Create one parent; the first four rings synthesize 502 responses.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param index: Index used by this test step.
    """

    ats = ats_factory.create(f"ts_nh{index}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    remap = f"map / http://127.0.0.1:{_origin.port}"
    if index < NUM_HOSTS - HOSTS_PER_RING:
        if not ats.plugin_exists("header_rewrite.so"):
            pytest.skip("header_rewrite.so is not installed")
        ats.write_config_file("hdr_rw.conf", "set-status 502\n")
        remap += " @plugin=header_rewrite.so @pparam=hdr_rw.conf"
    ats.remap_config.add_line(remap)
    _dns.add_records({f"next_hop_{index}": ["127.0.0.1"]})
    return ats


def configure_front_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _next_hops: list[ATS]) -> ATS:
    """Configure the maximum five ring groups and alternate-ring failover.

    :param _dns: Test-local dns configured by the test.
    :param _next_hops: Test-local next hops configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", return_code=(0, -2))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.cache.http": 0,
            "proxy.config.http.parent_proxy.per_parent_connect_attempts": 1,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
            "proxy.config.http.down_server.cache_time": 1,
            "proxy.config.http.parent_proxy.self_detect": 0,
        })
    lines = ["groups:"]
    index = 0
    for ring in range(NUM_RINGS):
        lines.append(f"  - &g{ring}")
        for _ in range(HOSTS_PER_RING):
            next_hop = _next_hops[index]
            lines.extend(
                (
                    f"    - host: next_hop_{index}",
                    "      protocol:",
                    "        - scheme: http",
                    f"          port: {next_hop.http_port}",
                    "      weight: 1.0",
                ))
            index += 1
    lines.extend(
        (
            "strategies:",
            "  - strategy: the-strategy",
            "    policy: consistent_hash",
            "    hash_key: path",
            "    go_direct: false",
            "    parent_is_proxy: true",
            "    ignore_self_detect: true",
            "    scheme: http",
            "    failover:",
            "      ring_mode: alternate_ring",
            "      max_simple_retries: 5",
            "      response_codes: [404]",
            "      max_unavailable_retries: 5",
            "      markdown_codes: [502]",
            "    groups:",
        ))
    lines.extend(f"      - *g{ring}" for ring in range(NUM_RINGS))
    ats.write_config_file("strategies.yaml", "\n".join(lines) + "\n")
    ats.remap_config.add_line("map http://dummy.com http://not_used @strategy=the-strategy")
    return ats


def request_all_objects(*, _ats: ATS, _curl: Curl) -> None:
    """Require each path to reach the successful final ring.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    for index in range(NUM_OBJECTS):
        result = _curl.run_for(
            _ats,
            f"--verbose --proxy '127.0.0.1:{_ats.http_port}' 'http://dummy.com/obj{index}'",
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "This is the body.\n"


def normalized_trace(*, _ats: ATS) -> str:
    """Normalize next-hop debug lines for the existing ring-walk gold.

    :param _ats: Test-local ats configured by the test.
    """

    lines = []
    for line in _ats.traffic_out.read_text(errors="replace").splitlines():
        if "ParentResultType::SPECIFIED" not in line:
            continue
        line = re.sub(r"^.*\(next_hop\) [^ ]* ", "", line)
        lines.append(re.sub(r"\.\d+$", "", line))
    return "\n".join(lines) + "\n"


def test_strategies_ch5(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Consistent hashing can traverse all five configured rings.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _dns = services.dns("dns")
    _next_hops = [configure_next_hop(ats_factory, index, _dns=_dns, _origin=_origin) for index in range(NUM_HOSTS)]
    _ats = configure_front_ats(ats_factory, _dns=_dns, _next_hops=_next_hops)

    _origin.start()
    _dns.start()
    for next_hop in _next_hops:
        next_hop.start()
    _ats.start()
    request_all_objects(_ats=_ats, _curl=curl)
    assert normalized_trace(_ats=_ats) == (TEST_DIRECTORY / "trace.gold").read_text()
