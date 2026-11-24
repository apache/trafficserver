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

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines

WARNING = "not pre-warming tunnel destination"

# Destinations whose port or host is filled in per connection, from a port
# template or a match group, so there is nothing to pre-warm. Each one logs
# WARNING once, including the one with two ALPN ids.
SKIPPED_SNI = (
    "  - fqdn: template.example.com\n"
    "    forward_route: 127.0.0.1:{inbound_local_port}\n"
    "    tunnel_prewarm: true\n"
    "  - fqdn: pp.example.com\n"
    "    partial_blind_route: 127.0.0.1:{proxy_protocol_port}\n"
    "    tunnel_alpn: [h2, http/1.1]\n"
    "    tunnel_prewarm: true\n"
    "  - fqdn: '*.group.example.com'\n"
    "    forward_route: 127.0.0.1:4$1\n"
    "    tunnel_prewarm: true\n"
    "  - fqdn: '*.host.example.com'\n"
    "    forward_route: $1.backend.example.com:443\n"
    "    tunnel_prewarm: true\n"
)
SKIPPED_DESTINATIONS = (
    "127.0.0.1:{inbound_local_port}",
    "127.0.0.1:{proxy_protocol_port}",
    "127.0.0.1:4$1",
    "$1.backend.example.com:443",
)


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Configure pre-warmed tunnel routes with fixed destinations and with filled-in ones.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _origin_port: Port for the fixed routes' destinations.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line("map / http://127.0.0.1:1/")
    sni_yaml = (
        "sni:\n"
        "  - fqdn: valid.example.com\n"
        f"    forward_route: 127.0.0.1:{_origin_port}\n"
        "    tunnel_prewarm: true\n"
        "  - fqdn: ipv6.example.com\n"
        f"    forward_route: '[::1]:{_origin_port}'\n"
        "    tunnel_prewarm: true\n"
        f"{SKIPPED_SNI}"
    )
    ats.write_config_file("sni.yaml", sni_yaml)
    return ats


def test_tls_tunnel_prewarm_destination(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A pre-warm destination filled in per connection is skipped at startup, and a fixed one is registered.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    origin_port = services.allocate_port()
    _ats = configure_ats(ats_factory, _origin_port=origin_port)

    _ats.start()
    diags = wait_for_file_lines(_ats.diags_log, WARNING, len(SKIPPED_DESTINATIONS))
    for destination in SKIPPED_DESTINATIONS:
        assert f'"{destination}"' in diags, diags
    assert diags.count(WARNING) == len(SKIPPED_DESTINATIONS), diags
    assert _ats.is_running

    # Only the two fixed routes register pre-warm stats: each destination has 7.
    stats = wait_for_file_lines(_ats.diags_log, "dynamic stats are registered for pre-warming tunnel", 1)
    assert re.search(r"\b14 dynamic stats are registered for pre-warming tunnel", stats), stats

    # Each fixed route is registered under the host and port a connection to it uses.
    for host in ("127.0.0.1", "::1"):
        metric = f"proxy.process.tunnel.prewarm.{host}:{origin_port}.tcp.total_miss"
        result = _ats.traffic_ctl("metric", "get", metric)
        assert result.returncode == 0 and metric in result.stdout, result.output
