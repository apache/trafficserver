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

from enum import Enum, auto
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory, VerifierServer

REPLAY_DIRECTORY = Path(__file__).parent / "replay"


class ConnectionType(Enum):
    """Inbound connection modes covered by the SNI ACL scenario."""

    GET = auto()
    TUNNEL = auto()
    PROXY = auto()


class SniIpAllowScenario:
    """Verify SNI access control for remapped, tunneled, and Proxy Protocol traffic."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        connection_type: ConnectionType,
    ) -> None:
        self._services = services
        self._connection_type = connection_type
        suffix = connection_type.name.lower()
        replay_name = {
            ConnectionType.GET: "ip_allow.replay.yaml",
            ConnectionType.TUNNEL: "ip_allow_tunnel.replay.yaml",
            ConnectionType.PROXY: "ip_allow_proxy.replay.yaml",
        }[connection_type]
        self._replay_file = REPLAY_DIRECTORY / replay_name
        self._dns = self.configure_dns(suffix)
        self._server = self.configure_server(suffix)
        self._ats = self.configure_ats(ats_factory, suffix)
        self._client = self.configure_client(suffix)

    def configure_dns(self, suffix: str) -> DNSServer:
        """Resolve all replay hostnames to the local verifier server."""

        return self._services.dns(f"dns-{suffix}", default="127.0.0.1")

    def configure_server(self, suffix: str) -> VerifierServer:
        """Create the verifier origin for one connection mode."""

        return self._services.verifier_server(f"server-{suffix}", self._replay_file)

    def configure_ats(self, ats_factory: ATSFactory, suffix: str) -> ATS:
        """Configure SNI ACLs and routing for one connection mode."""

        ats = ats_factory.create(f"ts-{suffix}", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)
        ats.add_default_ssl_files()
        sni_lines = [
            "sni:",
            "  - fqdn: block.me.com",
            "    ip_allow: 192.168.10.1",
        ]
        if self._connection_type is ConnectionType.TUNNEL:
            sni_lines.append(f"    tunnel_route: backend.server.com:{self._server.https_port}")
        if self._connection_type is ConnectionType.PROXY:
            sni_lines.extend(("  - fqdn: pp.block.me.com", "    ip_allow: 192.168.10.1"))
        sni_lines.extend(("  - fqdn: allow.me.com", "    ip_allow: 127.0.0.1"))
        if self._connection_type is ConnectionType.TUNNEL:
            sni_lines.append(f"    tunnel_route: backend.server.com:{self._server.https_port}")
        if self._connection_type is ConnectionType.PROXY:
            sni_lines.extend(("  - fqdn: pp.allow.me.com", "    ip_allow: 1.2.3.4"))
        ats.write_config_file("sni.yaml", "\n".join(sni_lines) + "\n")
        ats.remap_config.add_line(f"map / http://remapped.backend.server.com:{self._server.http_port}/")
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|ssl|proxyprotocol",
                "proxy.config.acl.subjects": "PROXY,PEER",
            })
        if self._connection_type is ConnectionType.TUNNEL:
            ats.records.update({"proxy.config.http.connect_ports": str(self._server.https_port)})
            ats.allow_private_connect()
        return ats

    def configure_client(self, suffix: str) -> ProcessService:
        """Create a verifier client whose first blocked connection is expected to fail."""

        if self._connection_type is ConnectionType.PROXY:
            http_ports = [self._ats.proxy_protocol_port]
            https_ports = [self._ats.proxy_protocol_https_port]
        else:
            http_ports = [self._ats.http_port]
            https_ports = [self._ats.https_port]
        return self._services.verifier_client(
            f"client-{suffix}",
            self._replay_file,
            http_ports=http_ports,
            https_ports=https_ports,
            return_code=1,
            allow_errors=True,
        )

    def run(self) -> None:
        """Run the blocked and allowed replay transactions and inspect both endpoints."""

        self._server.start()
        self._dns.start()
        self._ats.start()
        result = self._client.run()
        assert "allowed-response" in result.output
        assert "blocked-response" not in result.output
        assert "allowed-request" in self._server.output
        assert "blocked-request" not in self._server.output
        assert "block.me.com" not in self._server.output


@pytest.mark.parametrize("connection_type", tuple(ConnectionType), ids=lambda value: value.name.lower())
def test_tls_sni_ip_allow(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    connection_type: ConnectionType,
) -> None:
    """sni.yaml rejects disallowed peers before forwarding their traffic."""

    SniIpAllowScenario(ats_factory, services, connection_type).run()
