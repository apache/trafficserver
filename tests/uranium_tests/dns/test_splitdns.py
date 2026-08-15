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


class SplitDNSScenario:
    """Verify a split DNS rule resolves its selected origin hostname."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._dns = self.configure_dns(services)
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve the hostname selected by splitdns.config."""

        dns = services.dns("dns")
        dns.add_records({"foo.ts.a.o.": ["127.0.0.1"]})
        return dns

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Configure the shared origin response."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure split and literal-address remap rules."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.dns.splitDNS.enabled": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "dns|splitdns",
            })
        ats.splitdns_config.add_line(f"dest_domain=foo.ts.a.o named=127.0.0.1:{self._dns.port}")
        ats.remap_config.add_line(f"map /foo/ http://foo.ts.a.o:{self._origin.port}/")
        ats.remap_config.add_line(f"map /bar/ http://127.0.0.1:{self._origin.port}/")
        return ats

    def request(self, path: str) -> None:
        """Verify one remap path reaches the origin."""

        result = self._curl.get(self._ats, path, options=("--verbose",))
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.output
        assert "Server: ATS/" in result.output

    def run(self) -> None:
        """Compare split-DNS and literal-address origin routing."""

        self._dns.start()
        self._origin.start()
        self._ats.start()
        self.request("/foo/")
        self.request("/bar/")


def test_splitdns(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """splitdns.config selects its DNS server without affecting literal remaps."""

    SplitDNSScenario(ats_factory, services, curl).run()
