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

import time

from tools.uranium.services import ATS, ATSFactory, DNSServer, ServiceFactory, VerifierServer


def test_dns_down_nameserver(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A failed nameserver is probed again and becomes usable after recovery.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    _REPLAY = "replay/multiple_host_requests.replay.yaml"
    _RETRY_PERIOD = 5

    def configure_dns_servers(services: ServiceFactory) -> tuple[DNSServer, DNSServer, DNSServer]:
        """Create restartable DNS processes sharing one UDP port.

        :param services: Factory owning support services and their cleanup.
        """

        first = services.dns("dns-initial", default="127.0.0.1")
        second = services.dns("dns-first-retry", port=first.port, default="127.0.0.1")
        third = services.dns("dns-second-retry", port=first.port, default="127.0.0.1")
        return first, second, third

    def configure_origin(services: ServiceFactory) -> VerifierServer:
        """Configure the three hostname-specific origin transactions.

        :param services: Factory owning support services and their cleanup.
        """

        return services.verifier_server("origin", _REPLAY)

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Route each path through a distinct hostname on the shared origin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "hostdb|dns",
                "proxy.config.dns.nameservers": f"127.0.0.1:{_dns_servers[0].port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        for ordinal in ("first", "second", "third"):
            ats.remap_config.add_line(f"map /{ordinal}/host http://{ordinal}.host.com:{_origin.http_port}/")
        return ats

    def run_client(key: str, *, succeeds: bool) -> None:
        """Run one keyed transaction with DNS reachable or unavailable.

        :param key: Key used by this test step.
        :param succeeds: Succeeds used by this test step.
        """
        nonlocal _client_index

        _client_index += 1
        expected = 0 if succeeds else 1
        result = _services.verifier_client(
            f"client-{_client_index}",
            _REPLAY,
            http_ports=[_ats.http_port],
            keys=[key],
            return_code=expected,
            allow_errors=not succeeds,
        ).run()
        assert result.returncode == expected, result.output
        assert f"uuid: {key}" in result.output

    def wait_past_retry_period() -> None:
        """Wait until ATS is permitted to probe the nameserver again."""

        time.sleep(_RETRY_PERIOD + 1)

    _services = services
    _dns_servers = configure_dns_servers(services)
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)
    _client_index = 0

    initial_dns, first_retry_dns, second_retry_dns = _dns_servers
    _origin.start()
    initial_dns.start()
    _ats.start()
    run_client("first_host", succeeds=True)

    initial_dns.stop()
    run_client("second_host", succeeds=False)
    first_retry_dns.start()
    wait_past_retry_period()
    run_client("second_host", succeeds=True)

    first_retry_dns.stop()
    run_client("third_host", succeeds=False)
    wait_past_retry_period()
    second_retry_dns.start()
    wait_past_retry_period()
    run_client("third_host", succeeds=True)
