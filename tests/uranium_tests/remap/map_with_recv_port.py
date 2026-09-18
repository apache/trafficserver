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


class MapWithRecvPortScenario:
    """Select a remap rule according to the TCP or Unix receiving endpoint."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
        self._curl = curl
        self._use_yaml = use_yaml
        self._origin = self.configure_origin(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create distinct responses for TCP, Unix, and incorrect rule selection."""

        origin = services.origin("origin")
        for path, body in (("/ip", "ip"), ("/unix", "unix"), ("/error", "error")):
            origin.add_response(
                {
                    "headers": f"GET {path} HTTP/1.1\r\nHost: origin.example.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                    "body": body
                },
            )
        return origin

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve the remapped origin hostname locally."""

        return services.dns("dns", default="127.0.0.1")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure equivalent classic or YAML receiving-port rules."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        if self._use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    "    from:",
                    "      url: http://test.example.com",
                    "    to:",
                    f"      url: http://origin.example.com:{self._origin.port}/error",
                    "  - type: map_with_recv_port",
                    "    from:",
                    f"      url: http://test.example.com:{ats.http_port}/",
                    "    to:",
                    f"      url: http://origin.example.com:{self._origin.port}/ip",
                    "  - type: map_with_recv_port",
                    "    from:",
                    "      url: http+unix://test.example.com",
                    "    to:",
                    f"      url: http://origin.example.com:{self._origin.port}/unix",
                ])
        else:
            ats.remap_config.add_lines(
                [
                    f"map http://test.example.com http://origin.example.com:{self._origin.port}/error",
                    f"map_with_recv_port http://test.example.com:{ats.http_port}/ "
                    f"http://origin.example.com:{self._origin.port}/ip",
                    f"map_with_recv_port http+unix://test.example.com http://origin.example.com:{self._origin.port}/unix",
                ])
        return ats

    def run(self) -> None:
        """Send one request and verify the receiving-endpoint-specific response."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        result = self._curl.get(self._ats, headers={"Host": "test.example.com"}, options=f"--verbose")
        assert result.returncode == 0, result.output
        expected = "unix" if self._curl.uses_uds else "ip"
        assert result.stdout == expected, result.output
