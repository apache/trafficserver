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


def configure_ats(
        ats_factory: ATSFactory, *, _ports: tuple[int, ...], _server_one: VerifierServer, _server_three: VerifierServer,
        _server_two: VerifierServer) -> ATS:
    """Configure one unmapped listener and three port-aware SNI listeners.

    :param _ports: Test-local ports configured by the test.
    :param _server_one: Test-local server one configured by the test.
    :param _server_three: Test-local server three configured by the test.
    :param _server_two: Test-local server two configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    port_one, port_two, port_three, port_unmapped = _ports
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http.server_ports": (f"{port_one}:ssl {port_two}:ssl {port_three}:ssl {port_unmapped}:ssl"),
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "dns|http|ssl|sni",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server_three.http_port}")
    ats.allow_private_connect(("CONNECT", "GET"))
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: yay.example.com\n"
        f"    inbound_port_ranges: {port_one}-{port_one}\n"
        f"    tunnel_route: localhost:{_server_one.https_port}\n"
        "  - fqdn: yay.example.com\n"
        "    inbound_port_ranges:\n"
        f"      - {port_two}\n"
        f"      - {port_three}\n"
        f"    tunnel_route: localhost:{_server_two.https_port}\n",
    )
    return ats


def configure_client(name: str, port: int, key: str, *, _services: ServiceFactory) -> ProcessService:
    """Create a verifier client for one listener and transaction key.

    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param port: Allocated TCP or UDP listener port number.
    :param key: Key used by this test step.
    """

    return _services.verifier_client(name, REPLAY_FILE, https_ports=[port], keys=[key])


def observed(server: VerifierServer, key: str) -> bool:
    """Return whether @a server received the transaction body for @a key.

    :param server: Server used by this test.
    :param key: Key used by this test step.
    """

    expression = rf"Received (\(with headers\) )?an HTTP/1 (Content-Length )?body of 16 bytes for key {key}"
    return re.search(expression, server.output) is not None


def test_tls_sni_with_port(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """SNI inbound_port_ranges selects the intended tunnel route.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server_one = services.verifier_server("server-one", REPLAY_FILE)
    _server_two = services.verifier_server("server-two", REPLAY_FILE)
    _server_three = services.verifier_server("server-three", REPLAY_FILE)
    _ports = tuple(services.allocate_port() for _ in range(4))
    _ats = configure_ats(ats_factory, _ports=_ports, _server_one=_server_one, _server_three=_server_three, _server_two=_server_two)

    for server in (_server_one, _server_two, _server_three):
        server.start()
    _ats.start()
    port_one, port_two, port_three, port_unmapped = _ports

    configure_client("client-unmapped", port_unmapped, "conn_remapped", _services=services).run()
    assert not observed(_server_one, "conn_remapped")
    assert not observed(_server_two, "conn_remapped")
    assert observed(_server_three, "conn_remapped")

    configure_client("client-one", port_one, "conn_accepted", _services=services).run()
    assert observed(_server_one, "conn_accepted")
    assert not observed(_server_two, "conn_accepted")

    configure_client("client-two", port_two, "conn_accepted", _services=services).run()
    assert observed(_server_two, "conn_accepted")
    configure_client("client-three", port_three, "conn_accepted", _services=services).run()
    assert len(re.findall(r"key conn_accepted", _server_two.output)) >= 2

    diagnostics = _ats.diags_log.read_text(errors="replace")
    assert "unsupported key 'inbound_port_range'" not in diagnostics
    assert "not available in the map" in _ats.traffic_out.read_text(errors="replace")
