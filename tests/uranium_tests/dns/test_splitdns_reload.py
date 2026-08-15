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


class SplitDNSReloadScenario:
    """Verify ConfigRegistry reloads splitdns.config after it changes."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._dns = self.configure_dns(services)
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve the split DNS test hostname."""

        dns = services.dns("dns")
        dns.add_records({"foo.ts.a.o.": ["127.0.0.1"]})
        return dns

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Configure the origin response used before reload."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable split DNS and its reload diagnostics."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.dns.splitDNS.enabled": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "splitdns|config",
            })
        ats.splitdns_config.add_line(f"dest_domain=foo.ts.a.o named=127.0.0.1:{self._dns.port}")
        ats.remap_config.add_line(f"map /foo/ http://foo.ts.a.o:{self._origin.port}/")
        return ats

    def verify_startup_configuration(self) -> None:
        """Verify split DNS routes a request before reload."""

        result = self._curl.get(self._ats, "/foo/", options=("--verbose",))
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.output

    def reload(self) -> None:
        """Touch splitdns.config and wait for its ConfigRegistry task."""

        (self._ats.config_directory / "splitdns.config").touch()
        result = self._ats.traffic_ctl(
            "config",
            "reload",
            "--monitor",
            "--show-details",
            "--token",
            "splitdns-reload",
            "--initial-wait",
            "0.1",
            "--refresh-int",
            "0.1",
            "--timeout",
            "15s",
        )
        assert result.returncode == 0, result.output
        assert "splitdns.config" in result.stdout
        assert "success" in result.stdout

    def run(self) -> None:
        """Start services, verify routing, and reload splitdns.config."""

        self._dns.start()
        self._origin.start()
        self._ats.start()
        self.verify_startup_configuration()
        self.reload()


def test_splitdns_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A changed splitdns.config participates in ConfigRegistry reload."""

    SplitDNSReloadScenario(ats_factory, services, curl).run()
