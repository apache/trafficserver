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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class StaleNextHopScenario:
    """Revalidate a stale object through a next-hop strategy."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._dns = services.dns("dns")
        self._next_hop = self.configure_next_hop(ats_factory)
        self._ats = self.configure_front_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create a short-lived cacheable object."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET /obj0 HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=2\r\n\r\n",
                "body": "This is the body.\n",
            },
        )
        return origin

    def configure_next_hop(self, ats_factory: ATSFactory) -> ATS:
        """Create the single parent proxy."""

        ats = ats_factory.create("ts_nh0", return_code=(0, -2))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        self._dns.add_records({"next_hop0": ["127.0.0.1"]})
        return ats

    def configure_front_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure caching and the consistent-hash parent strategy."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.cache.http": 1,
                "proxy.config.http.uncacheable_requests_bypass_parent": 0,
                "proxy.config.http.no_dns_just_forward_to_parent": 1,
                "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
                "proxy.config.http.parent_proxy.self_detect": 0,
            })
        ats.write_config_file(
            "strategies.yaml",
            "groups:\n"
            "  - &g1\n"
            "    - host: next_hop0\n"
            "      protocol:\n"
            "        - scheme: http\n"
            f"          port: {self._next_hop.http_port}\n"
            "      weight: 1.0\n"
            "strategies:\n"
            "  - strategy: the-strategy\n"
            "    policy: consistent_hash\n"
            "    hash_key: path\n"
            "    go_direct: false\n"
            "    parent_is_proxy: true\n"
            "    ignore_self_detect: true\n"
            "    groups:\n"
            "      - *g1\n"
            "    scheme: http\n",
        )
        ats.remap_config.add_line("map http://dummy.com http://not_used @strategy=the-strategy")
        return ats

    def request_object(self) -> None:
        """Fetch the object through the front proxy."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--proxy",
            f"127.0.0.1:{self._ats.http_port}",
            "http://dummy.com/obj0",
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "This is the body.\n"

    def run(self) -> None:
        """Cache, age, and revalidate the object through the parent."""

        self._origin.start()
        self._dns.start()
        self._next_hop.start()
        self._ats.start()
        self.request_object()
        time.sleep(4)
        self.request_object()
        trace = self._next_hop.traffic_out.read_text(errors="replace")
        assert "Stale in cache" in trace


def test_strategies_stale(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A stale object is evaluated through the configured next-hop strategy."""

    StaleNextHopScenario(ats_factory, services, curl).run()
