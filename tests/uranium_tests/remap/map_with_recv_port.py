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


def run_map_with_recv_port(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
    """Select a remap rule according to the TCP or Unix receiving endpoint.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_yaml: Use yaml used by this test step.
    """

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create distinct responses for TCP, Unix, and incorrect rule selection.

        :param services: Factory owning support services and their cleanup.
        """

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

    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the remapped origin hostname locally.

        :param services: Factory owning support services and their cleanup.
        """

        return services.dns("dns", default="127.0.0.1")

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure equivalent classic or YAML receiving-port rules.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns",
                "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        if use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    "    from:",
                    "      url: http://test.example.com",
                    "    to:",
                    f"      url: http://origin.example.com:{_origin.port}/error",
                    "  - type: map_with_recv_port",
                    "    from:",
                    f"      url: http://test.example.com:{ats.http_port}/",
                    "    to:",
                    f"      url: http://origin.example.com:{_origin.port}/ip",
                    "  - type: map_with_recv_port",
                    "    from:",
                    "      url: http+unix://test.example.com",
                    "    to:",
                    f"      url: http://origin.example.com:{_origin.port}/unix",
                ])
        else:
            ats.remap_config.add_lines(
                [
                    f"map http://test.example.com http://origin.example.com:{_origin.port}/error",
                    f"map_with_recv_port http://test.example.com:{ats.http_port}/ "
                    f"http://origin.example.com:{_origin.port}/ip",
                    f"map_with_recv_port http+unix://test.example.com http://origin.example.com:{_origin.port}/unix",
                ])
        return ats

    _origin = configure_origin(services)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory)

    _origin.start()
    _dns.start()
    _ats.start()
    result = curl.get(_ats, headers={"Host": "test.example.com"}, options=f"--verbose")
    assert result.returncode == 0, result.output
    expected = "unix" if curl.uses_uds else "ip"
    assert result.stdout == expected, result.output
