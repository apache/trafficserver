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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create a short-lived cacheable object.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET /obj0 HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=2\r\n\r\n",
            "body": "This is the body.\n",
        },
    )
    return origin


def configure_next_hop(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Create the single parent proxy.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts_nh0", return_code=(0, -2))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    _dns.add_records({"next_hop0": ["127.0.0.1"]})
    return ats


def configure_front_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _next_hop: ATS) -> ATS:
    """Configure caching and the consistent-hash parent strategy.

    :param _dns: Test-local dns configured by the test.
    :param _next_hop: Test-local next hop configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.cache.http": 1,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
            "proxy.config.http.parent_proxy.self_detect": 0,
        })
    ats.write_config_file(
        "strategies.yaml",
        "groups:\n"
        "  - &g1\n"
        "    - host: next_hop0\n"
        "      protocol:\n"
        "        - scheme: http\n"
        f"          port: {_next_hop.http_port}\n"
        "      weight: 1.0\n"
        "strategies:\n"
        "  - strategy: the-strategy\n"
        "    policy: consistent_hash\n"
        "    hash_key: path\n"
        "    go_direct: false\n"
        "    parent_is_proxy: true\n"
        "    ignore_self_detect: true\n"
        "    groups:\n"
        "      - *g1\n"
        "    scheme: http\n",
    )
    ats.remap_config.add_line("map http://dummy.com http://not_used @strategy=the-strategy")
    return ats


def request_object(*, _ats: ATS, _curl: Curl) -> None:
    """Fetch the object through the front proxy.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        f"--verbose --proxy '127.0.0.1:{_ats.http_port}' http://dummy.com/obj0",
    )
    assert result.returncode == 0, result.output
    assert result.stdout == "This is the body.\n"


def test_strategies_stale(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A stale object is evaluated through the configured next-hop strategy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _dns = services.dns("dns")
    _next_hop = configure_next_hop(ats_factory, _dns=_dns, _origin=_origin)
    _ats = configure_front_ats(ats_factory, _dns=_dns, _next_hop=_next_hop)

    _origin.start()
    _dns.start()
    _next_hop.start()
    _ats.start()
    request_object(_ats=_ats, _curl=curl)
    time.sleep(4)
    request_object(_ats=_ats, _curl=curl)
    trace = _next_hop.traffic_out.read_text(errors="replace")
    assert "Stale in cache" in trace
