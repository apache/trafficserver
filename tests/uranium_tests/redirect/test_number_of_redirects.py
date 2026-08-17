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


class NumberOfRedirectsScenario:
    """Split a two-hop redirect chain between ATS and curl."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        redirect_limit: int,
    ) -> None:
        self._curl = curl
        self._redirect_limit = redirect_limit
        self._server1, self._server2, self._server3 = self.configure_origins(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origins(services: ServiceFactory) -> tuple[OriginServer, OriginServer, OriginServer]:
        """Create the two redirects and final 200 response."""

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

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve every redirect hostname inside the test sandbox."""

        dns = services.dns("dns")
        dns.add_records({name: ["127.0.0.1"] for name in ("a.test", "b.test", "c.test")})
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the requested internal redirect-following limit."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|redirect|http_redirect",
                "proxy.config.http.number_of_redirections": self._redirect_limit,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.http.redirect.actions": "self:follow",
            })
        ats.remap_config.add_lines(
            (
                f"map http://a.test/ping http://a.test:{self._server1.port}/ping",
                f"map http://b.test:{self._server2.port}/pong http://b.test:{self._server2.port}/pong",
                f"map http://c.test:{self._server3.port}/pang http://c.test:{self._server3.port}/pang",
            ))
        return ats

    def run(self) -> None:
        """Require curl to observe only redirects ATS did not follow itself."""

        for server in (self._server1, self._server2, self._server3):
            server.start()
        self._dns.start()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            (
                f"--location --verbose --proxy '127.0.0.1:{self._ats.http_port}' --header 'uuid: redirect_test_1' "
                f"http://a.test/ping"),
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stderr
        assert result.stderr.count("HTTP/1.1 302") == 2 - self._redirect_limit


@pytest.mark.parametrize("redirect_limit", (0, 1, 2))
def test_number_of_redirects(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    redirect_limit: int,
) -> None:
    """`number_of_redirections` controls how much of a chain ATS follows."""

    NumberOfRedirectsScenario(ats_factory, services, curl, redirect_limit).run()
