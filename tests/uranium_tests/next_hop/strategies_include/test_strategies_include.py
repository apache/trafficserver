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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

STRATEGIES_BODY = (
    "strategies:\n"
    "  - strategy: the-strategy\n"
    "    policy: first_live\n"
    "    go_direct: false\n"
    "    parent_is_proxy: false\n"
    "    ignore_self_detect: true\n"
    "    groups:\n"
    "      - *g1\n"
    "    scheme: http\n"
)

LEGACY_WARNING = (
    r"Deprecated '#include config/hosts\.yaml' in '.*/strategies\.yaml'; use '#include hosts\.yaml' instead\. "
    r"This fallback will be removed in 12\.0\.0\."
)


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the next-hop origin that the included hosts point at.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET /path HTTP/1.1\r\nHost: strategy.test\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
            "body": "This is the body.\n",
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, name: str, include_target: str, *, dns: DNSServer, origin: OriginServer) -> ATS:
    """Create a proxy whose strategy takes its host group from an included hosts.yaml.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Name of the Traffic Server instance.
    :param include_target: Path written after '#include' in strategies.yaml.
    :param dns: DNS server resolving the next-hop host name.
    :param origin: Origin serving as the strategy's only next hop.
    """

    ats = ats_factory.create(name)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "next_hop|url_rewrite",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.cache.http": 0,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.self_detect": 0,
        }
    )
    ats.write_config_file(
        "hosts.yaml",
        "groups:\n"
        "  - &g1\n"
        "    - host: nexthop.test\n"
        "      protocol:\n"
        "        - scheme: http\n"
        f"          port: {origin.port}\n"
        "      weight: 1.0\n",
    )
    ats.write_config_file("strategies.yaml", f"#include {include_target}\n" + STRATEGIES_BODY)
    ats.remap_config.add_line("map http://strategy.test http://not_used @strategy=the-strategy")
    return ats


# The traffic_server working directory is not the configuration directory, so a
# bare include only loads when it resolves relative to the configuration
# directory. The legacy spelling is relative to the install prefix, the parent of
# the configuration directory in the sandbox layout.
@pytest.mark.parametrize(
    ("include_target", "deprecation_expected"),
    [
        pytest.param("hosts.yaml", False, id="config_relative"),
        pytest.param("config/hosts.yaml", True, id="legacy_prefix_relative"),
    ],
)
def test_strategies_include(
    ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, include_target: str, deprecation_expected: bool
) -> None:
    """A strategy whose hosts come from an '#include' serves requests, warning only for the legacy spelling.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param include_target: Path written after '#include' in strategies.yaml.
    :param deprecation_expected: Whether loading the include logs the legacy-path deprecation warning.
    """

    origin = configure_origin(services)
    dns = services.dns("dns")
    dns.add_records({"nexthop.test": ["127.0.0.1"]})
    ats = configure_ats(ats_factory, "ts", include_target, dns=dns, origin=origin)

    origin.start()
    dns.start()
    ats.start()

    result = curl.run_for(ats, f"--verbose --proxy 127.0.0.1:{ats.http_port} http://strategy.test/path")
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.stderr, result.output

    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    if deprecation_expected:
        assert re.search(LEGACY_WARNING, diags), diags
    else:
        assert "Deprecated '#include" not in diags, diags
