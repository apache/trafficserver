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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the hostname selected by splitdns.config.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({"foo.ts.a.o.": ["127.0.0.1"]})
    return dns


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Configure the shared origin response.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Configure split and literal-address remap rules.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.dns.splitDNS.enabled": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "dns|splitdns",
        })
    ats.splitdns_config.add_line(f"dest_domain=foo.ts.a.o named=127.0.0.1:{_dns.port}")
    ats.remap_config.add_line(f"map /foo/ http://foo.ts.a.o:{_origin.port}/")
    ats.remap_config.add_line(f"map /bar/ http://127.0.0.1:{_origin.port}/")
    return ats


def request(path: str, *, _ats: ATS, _curl: Curl) -> None:
    """Verify one remap path reaches the origin.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    """

    result = _curl.get(_ats, path, options=f"--verbose")
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.output
    assert "Server: ATS/" in result.output


def test_splitdns(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """splitdns.config selects its DNS server without affecting literal remaps.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _dns = configure_dns(services)
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _origin=_origin)

    _dns.start()
    _origin.start()
    _ats.start()
    request("/foo/", _ats=_ats, _curl=curl)
    request("/bar/", _ats=_ats, _curl=curl)
