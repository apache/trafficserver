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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory


def configure_origins(services: ServiceFactory) -> tuple[OriginServer, OriginServer, OriginServer]:
    """Create the two redirects and final 200 response.

    :param services: Factory owning support services and their cleanup.
    """

    server1 = services.origin("server1")
    server2 = services.origin("server2")
    server3 = services.origin("server3")
    server1.add_response(
        {
            "headers": "GET /ping HTTP/1.1\r\nuuid: redirect_test_1\r\nHost: a.test\r\n\r\n",
            "body": ""
        },
        {
            "headers":
                (
                    f"HTTP/1.1 302 Redirect\r\nLocation: http://b.test:{server2.port}/pong\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n"),
            "body": "",
        },
    )
    server2.add_response(
        {
            "headers": "GET /pong HTTP/1.1\r\nuuid: redirect_test_1\r\nHost: b.test\r\n\r\n",
            "body": ""
        },
        {
            "headers":
                (
                    f"HTTP/1.1 302 Redirect\r\nLocation: http://c.test:{server3.port}/pang\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n"),
            "body": "",
        },
    )
    server3.add_response(
        {
            "headers": "GET /pang HTTP/1.1\r\nuuid: redirect_test_1\r\nHost: c.test\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
            "body": ""
        },
    )
    return server1, server2, server3


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve every redirect hostname inside the test sandbox.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({name: ["127.0.0.1"] for name in ("a.test", "b.test", "c.test")})
    return dns


def configure_ats(
        ats_factory: ATSFactory, *, _dns: DNSServer, _redirect_limit: int, _server1: OriginServer, _server2: OriginServer,
        _server3: OriginServer) -> ATS:
    """Configure the requested internal redirect-following limit.

    :param _dns: Test-local dns configured by the test.
    :param _redirect_limit: Test-local redirect limit configured by the test.
    :param _server1: Test-local server1 configured by the test.
    :param _server2: Test-local server2 configured by the test.
    :param _server3: Test-local server3 configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|redirect|http_redirect",
            "proxy.config.http.number_of_redirections": _redirect_limit,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.url_remap.remap_required": 0,
            "proxy.config.http.redirect.actions": "self:follow",
        })
    ats.remap_config.add_lines(
        (
            f"map http://a.test/ping http://a.test:{_server1.port}/ping",
            f"map http://b.test:{_server2.port}/pong http://b.test:{_server2.port}/pong",
            f"map http://c.test:{_server3.port}/pang http://c.test:{_server3.port}/pang",
        ))
    return ats


@pytest.mark.parametrize("redirect_limit", (0, 1, 2))
def test_number_of_redirects(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    redirect_limit: int,
) -> None:
    """`number_of_redirections` controls how much of a chain ATS follows.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param redirect_limit: Redirect limit used by this test step.
    """
    _server1, _server2, _server3 = configure_origins(services)
    _dns = configure_dns(services)
    _ats = configure_ats(
        ats_factory, _dns=_dns, _redirect_limit=redirect_limit, _server1=_server1, _server2=_server2, _server3=_server3)

    for server in (_server1, _server2, _server3):
        server.start()
    _dns.start()
    _ats.start()
    result = curl.run_for(
        _ats,
        (f"--location --verbose --proxy '127.0.0.1:{_ats.http_port}' --header 'uuid: redirect_test_1' "
         f"http://a.test/ping"),
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.stderr
    assert result.stderr.count("HTTP/1.1 302") == 2 - redirect_limit
