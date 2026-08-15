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


class DnsTtlScenario:
    """Verify expired DNS entries are rejected or served stale as configured."""

    SUCCESS_REPLAY = "replay/single_transaction.replay.yaml"
    ERROR_REPLAY = "replay/server_error.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, serve_stale_for: int | None) -> None:
        self._services = services
        self._serve_stale_for = serve_stale_for
        self._dns = self.configure_dns(services)
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve the origin while the DNS process is running."""

        dns = services.dns("dns")
        dns.add_records({"resolve.this.com": ["127.0.0.1"]})
        return dns

    def configure_origin(self, services: ServiceFactory) -> VerifierServer:
        """Configure the reusable successful origin transaction."""

        return services.verifier_server("origin", self.SUCCESS_REPLAY)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a one-second DNS TTL and lookup timeout."""

        ats = ats_factory.create("ts", enable_cache=False)
        records: dict[str, object] = {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "dns",
            "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.hostdb.ttl_mode": 1,
            "proxy.config.hostdb.timeout": 1,
            "proxy.config.hostdb.lookup_timeout": 1,
        }
        if self._serve_stale_for is not None:
            records["proxy.config.hostdb.serve_stale_for"] = self._serve_stale_for
        ats.records.update(records)
        ats.remap_config.add_line(f"map / http://resolve.this.com:{self._origin.http_port}/")
        return ats

    def run_client(self, name: str, replay: str) -> None:
        """Run one Proxy Verifier client and require its expectations."""

        result = self._services.verifier_client(name, replay, http_ports=[self._ats.http_port]).run()
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Prime DNS, expire it with DNS down, and verify stale policy."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        self.run_client("prime-client", self.SUCCESS_REPLAY)

        self._dns.stop()
        time.sleep(3)
        expected = self.SUCCESS_REPLAY if self._serve_stale_for == 300 else self.ERROR_REPLAY
        self.run_client("expired-client", expected)


@pytest.mark.parametrize(
    "serve_stale_for",
    [None, 300, 1],
    ids=["stale-disabled", "within-stale-window", "beyond-stale-window"],
)
def test_dns_ttl(ats_factory: ATSFactory, services: ServiceFactory, serve_stale_for: int | None) -> None:
    """DNS TTL expiry honors the configured serve-stale window."""

    DnsTtlScenario(ats_factory, services, serve_stale_for).run()
