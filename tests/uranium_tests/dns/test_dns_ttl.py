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

import pytest

from tools.uranium.services import ATS, ATSFactory, DNSServer, ServiceFactory, VerifierServer

DNS_TTL_ERROR_REPLAY = "replay/server_error.replay.yaml"

DNS_TTL_SUCCESS_REPLAY = "replay/single_transaction.replay.yaml"


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the origin while the DNS process is running.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({"resolve.this.com": ["127.0.0.1"]})
    return dns


def configure_origin(services: ServiceFactory) -> VerifierServer:
    """Configure the reusable successful origin transaction.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("origin", DNS_TTL_SUCCESS_REPLAY)


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: VerifierServer, _serve_stale_for: int | None) -> ATS:
    """Configure a one-second DNS TTL and lookup timeout.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _serve_stale_for: Test-local serve stale for configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    records: dict[str, object] = {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "dns",
        "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
        "proxy.config.dns.resolv_conf": "NULL",
        "proxy.config.hostdb.ttl_mode": 1,
        "proxy.config.hostdb.timeout": 1,
        "proxy.config.hostdb.lookup_timeout": 1,
    }
    if _serve_stale_for is not None:
        records["proxy.config.hostdb.serve_stale_for"] = _serve_stale_for
    ats.records.update(records)
    ats.remap_config.add_line(f"map / http://resolve.this.com:{_origin.http_port}/")
    return ats


def run_client(name: str, replay: str, *, _ats: ATS, _services: ServiceFactory) -> None:
    """Run one Proxy Verifier client and require its expectations.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param replay: Replay used by this test step.
    """

    result = _services.verifier_client(name, replay, http_ports=[_ats.http_port]).run()
    assert result.returncode == 0, result.output


@pytest.mark.parametrize(
    "serve_stale_for",
    [None, 300, 1],
    ids=["stale-disabled", "within-stale-window", "beyond-stale-window"],
)
def test_dns_ttl(ats_factory: ATSFactory, services: ServiceFactory, serve_stale_for: int | None) -> None:
    """DNS TTL expiry honors the configured serve-stale window.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param serve_stale_for: Serve stale for used by this test step.
    """
    _dns = configure_dns(services)
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _origin=_origin, _serve_stale_for=serve_stale_for)

    _origin.start()
    _dns.start()
    _ats.start()
    run_client("prime-client", DNS_TTL_SUCCESS_REPLAY, _ats=_ats, _services=services)

    _dns.stop()
    time.sleep(3)
    expected = DNS_TTL_SUCCESS_REPLAY if serve_stale_for == 300 else DNS_TTL_ERROR_REPLAY
    run_client("expired-client", expected, _ats=_ats, _services=services)
