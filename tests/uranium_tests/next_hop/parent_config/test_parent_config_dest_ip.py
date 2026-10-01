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

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the health response and cacheable object.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /foo.txt HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=60\r\n\r\n",
            "body": "This is the body for foo.txt\n",
        },
    )
    return origin


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve both ATS layers and the synthetic destination.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({
        "origin": ["127.0.0.1"],
        "ts1": ["127.0.0.1"],
        "ts0": ["127.0.0.1"],
        "foo.bar": ["142.250.72.14"],
    })
    return dns


def common_records(proxy_name: str, *, _dns: DNSServer) -> dict[str, object]:
    """Return DNS and parent-selection records shared by both layers.

    :param _dns: Test-local dns configured by the test.
    :param proxy_name: Proxy name used by this test step.
    """

    return {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|dns|hostdb|parent",
        "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
        "proxy.config.dns.resolv_conf": "NULL",
        "proxy.config.hostdb.lookup_timeout": 2,
        "proxy.config.http.connect_attempts_timeout": 1,
        "proxy.config.http.parent_proxy.self_detect": 0,
        "proxy.config.http.insert_response_via_str": 1,
        "proxy.config.proxy_name": proxy_name,
    }


def configure_mid(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Configure the parent layer that maps to the origin by DNS name.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts1")
    ats.remap_config.add_line(f"map / http://origin:{_origin.port}")
    ats.records.update(common_records("ts1", _dns=_dns))
    return ats


def configure_edge(ats_factory: ATSFactory, *, _dns: DNSServer, _mid: ATS) -> ATS:
    """Configure the edge with adjacent dest_ip and dest_host rules.

    :param _dns: Test-local dns configured by the test.
    :param _mid: Test-local mid configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts0")
    ats.remap_config.add_line("map http://foo.bar http://foo.bar")
    ats.records.update(common_records("ts0", _dns=_dns))
    ats.parent_config.add_lines(
        (
            "dest_ip=93.184.216.34 port=80 go_direct=true",
            f'dest_host=foo.bar port=80 parent="ts1:{_mid.http_port}|1;" go_direct="false" parent_is_proxy="true"',
        ))
    return ats


def test_parent_config_dest_ip(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """parent.config dest_ip matching does not corrupt later destination DNS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("explicit forward-proxy coverage requires a TCP listener")
    _origin = configure_origin(services)
    _dns = configure_dns(services)
    _mid = configure_mid(ats_factory, _dns=_dns, _origin=_origin)
    _edge = configure_edge(ats_factory, _dns=_dns, _mid=_mid)

    _origin.start()
    _dns.start()
    _mid.start()
    _edge.start()
    result = curl.run(
        (
            f"--silent --dump-header /dev/stdout --output /dev/stderr --proxy "
            f"'http://127.0.0.1:{_edge.http_port}' http://foo.bar/foo.txt"),)
    assert result.returncode == 0, result.output
    assert re.search(r"Via:.* ts1 .* ts0 ", result.stdout), result.output
