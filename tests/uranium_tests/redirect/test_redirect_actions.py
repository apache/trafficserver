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

from collections.abc import Mapping
import json
import re
import socket
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, DNSServer, OriginServer, ServiceFactory, send_tcp

ACTION_STATUS = {
    "return": "HTTP/1.1 307 Temporary Redirect",
    "reject": "HTTP/1.1 403 Forbidden",
    "follow": "HTTP/1.1 204 No Content",
    "break": "HTTP/1.1 500 Cannot find server.",
}

SCENARIOS: tuple[dict[str, str], ...] = (
    {
        "private": "reject",
        "loopback": "follow",
        "multicast": "reject",
        "linklocal": "return",
        "routable": "reject",
        "self": "return",
        "default": "reject",
    },
    {
        "private": "return",
        "loopback": "follow",
        "multicast": "return",
        "linklocal": "reject",
        "routable": "return",
        "self": "reject",
        "default": "return",
    },
    {
        "loopback": "return",
        "default": "reject"
    },
    {
        "loopback": "reject",
        "default": "return"
    },
    {
        "default": "return"
    },
)


class RedirectActionsScenario:
    """Apply one redirect action table to every address class."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, scenario: Mapping[str, str]) -> None:
        self._scenario = scenario
        self._targets = self.discover_targets()
        self._origin = services.origin("origin", ip="0.0.0.0")
        self._dns = self.configure_dns(services)
        self.configure_origin()
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def discover_targets() -> dict[str, tuple[str, ...]]:
        """Return representative IPv4/IPv6 addresses, including this test host."""

        host = socket.gethostname()
        ipv4 = {
            address for family, _, _, _, (address, *_) in socket.getaddrinfo(host, None)
            if family == socket.AF_INET and not address.startswith("127.")
        }
        ipv6 = {
            f"[{address.split('%')[0]}]" for family, _, _, _, (address, *_) in socket.getaddrinfo(host, None)
            if family == socket.AF_INET6 and not address.lower().startswith("fe80")
        }
        if not ipv4:
            result = subprocess.run(("ip", "-json", "address", "show"), capture_output=True, text=True, check=False)
            if result.returncode == 0:
                for interface in json.loads(result.stdout):
                    if interface.get("link_type") == "loopback":
                        continue
                    for address in interface.get("addr_info", []):
                        if address.get("family") == "inet":
                            ipv4.add(address["local"])
                        elif address.get("family") == "inet6" and address.get("scope") != "link":
                            ipv6.add(f"[{address['local']}]")
        return {
            "private": ("10.0.0.1", "[fc00::1]"),
            "loopback": ("127.1.2.3",),
            "multicast": ("224.1.2.3", "[ff42::]"),
            "linklocal": ("169.254.0.1", "[fe80::]"),
            "routable": ("72.30.35.10", "[2001:4998:58:1836::10]"),
            "self": tuple(sorted(ipv4 | ipv6)),
        }

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Map the initial origin and every redirect hostname to its target address."""

        dns = services.dns("dns")
        records: dict[str, list[str]] = {"iwillredirect.test": ["127.0.0.1"]}
        for category, addresses in self._targets.items():
            for index, address in enumerate(addresses):
                records[self.domain(category, index)] = [address.strip("[]")]
        dns.add_records(records)
        return dns

    @staticmethod
    def domain(category: str, index: int) -> str:
        """Return a stable redirect hostname for one representative address."""

        return f"redirect-{category}-{index}.test"

    def configure_origin(self) -> None:
        """Return redirects for every target and content for followed loopback redirects."""

        self._origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: ignored\r\n\r\n"},
            {"headers": "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"},
        )
        for category, addresses in self._targets.items():
            for index, address in enumerate(addresses):
                path = f"/redirect/{category}/{index}"
                self._origin.add_response(
                    {"headers": f"GET {path} HTTP/1.1\r\nHost: ignored\r\n\r\n"},
                    {
                        "headers":
                            (
                                "HTTP/1.1 307 Temporary Redirect\r\n"
                                f"Location: http://{self.domain(category, index)}:{self._origin.port}/\r\n"
                                "Connection: close\r\n\r\n")
                    },
                )
        self._origin.add_response(
            {"headers": "GET /redirect/unresolved HTTP/1.1\r\nHost: ignored\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 307 Temporary Redirect\r\n"
                        f"Location: http://redirect-unresolved.test:{self._origin.port}/\r\n"
                        "Connection: close\r\n\r\n")
            },
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure ATS with the selected class-to-action mapping."""

        ats = ats_factory.create("ts", enable_cache=False)
        config = ",".join(f"{category}:{action}" for category, action in sorted(self._scenario.items()))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|redirect",
                "proxy.config.http.number_of_redirections": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.http.redirect.actions": config,
                "proxy.config.http.connect_attempts_timeout": 5,
                "proxy.config.http.connect_attempts_max_retries": 0,
            })
        return ats

    def request(self, path: str) -> str:
        """Send one raw request so the exact HTTP/1 status line remains visible."""

        return send_tcp(
            self._ats.http_port,
            f"GET {path} HTTP/1.1\r\nHost: iwillredirect.test:{self._origin.port}\r\nConnection: close\r\n\r\n",
            address="127.0.0.1",
            timeout=10,
        )

    def run(self) -> None:
        """Verify the configured result for every address class and an unresolved name."""

        self._dns.start()
        self._origin.start()
        self._ats.start()
        for category, addresses in self._targets.items():
            action = self._scenario.get(category, self._scenario["default"])
            for index, _ in enumerate(addresses):
                response = self.request(f"/redirect/{category}/{index}")
                assert response.startswith(ACTION_STATUS[action]), response
        unresolved = self.request("/redirect/unresolved")
        assert unresolved.startswith(ACTION_STATUS["break"]), unresolved


@pytest.mark.parametrize("scenario", SCENARIOS)
def test_redirect_actions(ats_factory: ATSFactory, services: ServiceFactory, scenario: Mapping[str, str]) -> None:
    """Redirect actions return, reject, or follow targets according to address class."""

    RedirectActionsScenario(ats_factory, services, scenario).run()
