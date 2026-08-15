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

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class ConnectDestinationAclScenario:
    """Apply outbound ip_allow rules to CONNECT and SNI tunnel targets."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("CONNECT destination ACL coverage requires a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._default = self.configure_ats(ats_factory, "default")
        self._allowed = self.configure_ats(ats_factory, "allowed", allow_loopback=True)
        self._sni = self.configure_sni_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the TLS endpoint used by permitted tunnels."""

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{origin.https_port}\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str, *, allow_loopback: bool = False) -> ATS:
        """Configure one forward proxy and optional loopback outbound allow."""

        ats = ats_factory.create(f"ts-{name}", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|ip_allow",
                "proxy.config.http.connect_ports": str(self._origin.https_port),
                "proxy.config.url_remap.remap_required": 0,
            })
        if allow_loopback:
            ats.ip_allow_config.add_lines(
                (
                    "ip_allow:",
                    "  - apply: in",
                    "    ip_addrs: 127.0.0.1",
                    "    action: allow",
                    "    methods: ALL",
                    "  - apply: out",
                    "    ip_addrs: 127.0.0.1",
                    "    action: allow",
                    "    methods: CONNECT",
                    "  - apply: out",
                    "    ip_addrs:",
                    "      - 0.0.0.0/8",
                    "      - 127.0.0.0/8",
                    '      - "::"',
                    "      - ::1",
                    "      - 10.0.0.0/8",
                    "      - 172.16.0.0/12",
                    "      - 192.168.0.0/16",
                    "      - 169.254.0.0/16",
                    "      - ::/96",
                    "      - fc00::/7",
                    "      - fe80::/10",
                    "      - ::ffff:0:0/96",
                    "    action: deny",
                    "    methods: CONNECT",
                ))
        return ats

    def configure_sni_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure an SNI tunnel route to the prohibited loopback target."""

        ats = ats_factory.create("ts-sni-default", enable_cache=False, enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|ip_allow|ssl|sni",
                "proxy.config.http.connect_ports": str(self._origin.https_port),
            })
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: sni-denied.example.com\n"
            f"    tunnel_route: 127.0.0.1:{self._origin.https_port}\n",
        )
        return ats

    def connect(self, ats: ATS, url: str) -> CommandResult:
        """Attempt one CONNECT request and report curl's status fields."""

        return self._curl.run_for(
            ats,
            "--silent",
            "--insecure",
            "--noproxy",
            "does-not-match",
            "--proxy",
            f"http://127.0.0.1:{ats.http_port}",
            "--output",
            "/dev/null",
            "--write-out",
            "http_code=%{http_code} http_connect=%{http_connect}\n",
            url,
        )

    def verify_denied_targets(self) -> None:
        """Verify prohibited and syntactically invalid CONNECT targets."""

        port = self._origin.https_port
        cases = (
            (f"https://127.0.0.1:{port}/", "403"),
            (f"https://0.0.0.0:{port}/", "400"),
            (f"https://0.1.2.3:{port}/", "403"),
            (f"https://[::]:{port}/", "400"),
            (f"https://[::7f00:1]:{port}/", "403"),
            (f"https://[::ffff:127.0.0.1]:{port}/", "403"),
            (f"https://[::ffff:7f00:1]:{port}/", "403"),
        )
        for url, status in cases:
            result = self.connect(self._default, url)
            assert result.returncode in (7, 56), result.output
            assert f"http_code=000 http_connect={status}" in result.stdout

    def verify_sni_denial(self) -> None:
        """Verify outbound policy also applies to SNI tunnel routes."""

        result = self._curl.run_for(
            self._sni,
            "--silent",
            "--insecure",
            "--verbose",
            "--resolve",
            f"sni-denied.example.com:{self._sni.https_port}:127.0.0.1",
            f"https://sni-denied.example.com:{self._sni.https_port}/",
        )
        assert result.returncode in (35, 52, 56), result.output
        diagnostics = self._sni.diags_log.read_text(errors="replace")
        assert re.search(r"server '127\.0\.0\.1.*' prohibited by ip-allow policy", diagnostics)

    def run(self) -> None:
        """Exercise default denials and an explicit outbound allow."""

        self._origin.start()
        self._default.start()
        self._allowed.start()
        self._sni.start()
        self.verify_denied_targets()
        self.verify_sni_denial()

        result = self.connect(self._allowed, f"https://127.0.0.1:{self._origin.https_port}/")
        assert result.returncode == 0, result.output
        assert "http_code=200 http_connect=200" in result.stdout


def test_connect_destination_acl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Outbound ip_allow protects CONNECT and SNI tunnel destinations."""

    ConnectDestinationAclScenario(ats_factory, services, curl).run()
