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
from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

NUM_OBJECTS = 32
NUM_NEXT_HOPS = 8
EXPECTED_TRACE_COUNTS = (18, 21, 9, 24, 3, 12, 3, 6)


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create all objects served behind the next-hop proxies.

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
    """Create one parent proxy and its DNS hostname.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param index: Index used by this test step.
    """

    ats = ats_factory.create(f"ts_nh{index}", return_code=(0, -2))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    _dns.add_records({f"next_hop{index}": ["127.0.0.1"]})
    return ats


def configure_front_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _next_hops: list[ATS]) -> ATS:
    """Configure the consistent-hash strategy under test.

    :param _dns: Test-local dns configured by the test.
    :param _next_hops: Test-local next hops configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.cache.http": 0,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
            "proxy.config.http.parent_proxy.self_detect": 0,
        })
    groups = ["groups:", "  - &g1"]
    for index, next_hop in enumerate(_next_hops):
        groups.extend(
            (
                f"    - host: next_hop{index}",
                "      protocol:",
                "        - scheme: http",
                f"          port: {next_hop.http_port}",
                "      weight: 1.0",
            ))
    strategies = (
        "strategies:\n"
        "  - strategy: the-strategy\n"
        "    policy: consistent_hash\n"
        "    hash_key: path\n"
        "    go_direct: false\n"
        "    parent_is_proxy: true\n"
        "    ignore_self_detect: true\n"
        "    groups:\n"
        "      - *g1\n"
        "    scheme: http\n")
    ats.write_config_file("strategies.yaml", "\n".join(groups) + "\n" + strategies)
    ats.remap_config.add_line("map http://dummy.com http://not_used @strategy=the-strategy")
    return ats


def request_all_objects(*, _ats: ATS, _curl: Curl) -> None:
    """Send every path through the front proxy.

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


def check_distribution(*, _next_hops: list[ATS]) -> None:
    """Compare each parent proxy's handled-response count with the gold ring.

    :param _next_hops: Test-local next hops configured by the test.
    """

    actual = tuple(next_hop.traffic_out.read_text(errors="replace").count("HTTP/1.1 200 OK") for next_hop in _next_hops)
    assert actual == EXPECTED_TRACE_COUNTS


def test_strategies_ch(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Consistent hashing selects the same parent distribution for known paths.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _dns = services.dns("dns")
    _next_hops = [configure_next_hop(ats_factory, index, _dns=_dns, _origin=_origin) for index in range(NUM_NEXT_HOPS)]
    _ats = configure_front_ats(ats_factory, _dns=_dns, _next_hops=_next_hops)

    _origin.start()
    _dns.start()
    for next_hop in _next_hops:
        next_hop.start()
    _ats.start()
    request_all_objects(_ats=_ats, _curl=curl)
    check_distribution(_next_hops=_next_hops)
