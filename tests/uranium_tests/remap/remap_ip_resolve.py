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


def run_remap_ip_resolve(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
    """Override global IPv4 resolution with per-remap IPv4 and IPv6 policies.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_yaml: Use yaml used by this test step.
    """

    def configure_origins(services: ServiceFactory) -> tuple[OriginServer, OriginServer]:
        """Create origins bound exclusively to IPv4 and IPv6 loopback.

        :param services: Factory owning support services and their cleanup.
        """

        ipv4 = services.origin("origin-ipv4", ip="127.0.0.1")
        ipv6 = services.origin("origin-ipv6", ip="::1")
        for origin, body in ((ipv4, "ipv4"), (ipv6, "ipv6")):
            origin.add_response(
                {
                    "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                    "body": body
                },
            )
        return ipv4, ipv6

    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Publish address families that let each override prove its policy.

        :param services: Factory owning support services and their cleanup.
        """

        dns = services.dns("dns")
        dns.add_records({
            "test.ipv4.only.com.": ["127.0.0.1"],
            "test.ipv6.only.com.": ["127.0.0.1", "::1"],
        })
        return dns

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure the two per-remap conf_remap resolution policies.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("conf_remap.so"):
            pytest.skip("conf_remap.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|conf_remap",
                "proxy.config.http.referer_filter": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.hostdb.ip_resolve": "ipv4",
            })
        if use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    "    from: {url: 'http://testDNS.com'}",
                    f"    to: {{url: 'http://test.ipv4.only.com:{_ipv4_origin.port}'}}",
                    "    plugins:",
                    "      - name: conf_remap.so",
                    "        params: ['proxy.config.hostdb.ip_resolve=ipv6;ipv4;client']",
                    "  - type: map",
                    "    from: {url: 'http://testDNS2.com'}",
                    f"    to: {{url: 'http://test.ipv6.only.com:{_ipv6_origin.port}'}}",
                    "    plugins:",
                    "      - name: conf_remap.so",
                    "        params: ['proxy.config.hostdb.ip_resolve=ipv6;only']",
                ])
        else:
            ats.remap_config.add_lines(
                [
                    f"map http://testDNS.com http://test.ipv4.only.com:{_ipv4_origin.port} "
                    "@plugin=conf_remap.so @pparam=proxy.config.hostdb.ip_resolve=ipv6;ipv4;client",
                    f"map http://testDNS2.com http://test.ipv6.only.com:{_ipv6_origin.port} "
                    "@plugin=conf_remap.so @pparam=proxy.config.hostdb.ip_resolve=ipv6;only",
                ])
        return ats

    def request(host: str, expected: str) -> None:
        """Send one hostname case and verify the selected address family.

        :param host: HTTP host name used for the request.
        :param expected: Expected result for this case.
        """

        result = curl.get(_ats, headers={"Host": host}, options=f"--verbose")
        assert result.returncode == 0, result.output
        assert result.stdout == expected, result.output

    _ipv4_origin, _ipv6_origin = configure_origins(services)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory)

    _ipv4_origin.start()
    _ipv6_origin.start()
    _dns.start()
    _ats.start()
    request("testDNS.com", "ipv4")
    request("testDNS2.com", "ipv6")
