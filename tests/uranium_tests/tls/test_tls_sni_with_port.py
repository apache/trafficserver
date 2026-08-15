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

from pathlib import Path
import re

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

REPLAY_FILE = Path(__file__).parent / "tls_sni_with_port.replay.yaml"


class SniWithPortScenario:
    """Route one SNI name differently based on the inbound listener port."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server_one = services.verifier_server("server-one", REPLAY_FILE)
        self._server_two = services.verifier_server("server-two", REPLAY_FILE)
        self._server_three = services.verifier_server("server-three", REPLAY_FILE)
        self._ports = tuple(services.allocate_port() for _ in range(4))
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure one unmapped listener and three port-aware SNI listeners."""

        port_one, port_two, port_three, port_unmapped = self._ports
        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.http.server_ports": (f"{port_one}:ssl {port_two}:ssl {port_three}:ssl {port_unmapped}:ssl"),
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "dns|http|ssl|sni",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server_three.http_port}")
        ats.allow_private_connect(("CONNECT", "GET"))
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: yay.example.com\n"
            f"    inbound_port_ranges: {port_one}-{port_one}\n"
            f"    tunnel_route: localhost:{self._server_one.https_port}\n"
            "  - fqdn: yay.example.com\n"
            "    inbound_port_ranges:\n"
            f"      - {port_two}\n"
            f"      - {port_three}\n"
            f"    tunnel_route: localhost:{self._server_two.https_port}\n",
        )
        return ats

    def configure_client(self, name: str, port: int, key: str) -> ProcessService:
        """Create a verifier client for one listener and transaction key."""

        return self._services.verifier_client(name, REPLAY_FILE, https_ports=[port], keys=[key])

    @staticmethod
    def observed(server: VerifierServer, key: str) -> bool:
        """Return whether @a server received the transaction body for @a key."""

        expression = rf"Received (\(with headers\) )?an HTTP/1 (Content-Length )?body of 16 bytes for key {key}"
        return re.search(expression, server.output) is not None

    def run(self) -> None:
        """Verify unmapped, single-port, and multi-port route behavior."""

        for server in (self._server_one, self._server_two, self._server_three):
            server.start()
        self._ats.start()
        port_one, port_two, port_three, port_unmapped = self._ports

        self.configure_client("client-unmapped", port_unmapped, "conn_remapped").run()
        assert not self.observed(self._server_one, "conn_remapped")
        assert not self.observed(self._server_two, "conn_remapped")
        assert self.observed(self._server_three, "conn_remapped")

        self.configure_client("client-one", port_one, "conn_accepted").run()
        assert self.observed(self._server_one, "conn_accepted")
        assert not self.observed(self._server_two, "conn_accepted")

        self.configure_client("client-two", port_two, "conn_accepted").run()
        assert self.observed(self._server_two, "conn_accepted")
        self.configure_client("client-three", port_three, "conn_accepted").run()
        assert len(re.findall(r"key conn_accepted", self._server_two.output)) >= 2

        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "unsupported key 'inbound_port_range'" not in diagnostics
        assert "not available in the map" in self._ats.traffic_out.read_text(errors="replace")


def test_tls_sni_with_port(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """SNI inbound_port_ranges selects the intended tunnel route."""

    SniWithPortScenario(ats_factory, services).run()
