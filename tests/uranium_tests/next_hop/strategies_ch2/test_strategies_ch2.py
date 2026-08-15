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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
NUM_OBJECTS = 32
NUM_NEXT_HOPS = 8
INITIAL_DOWN = {3, 6}


class ConsistentHashFailoverScenario:
    """Walk the hash ring while two parents are down, then restore them."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._dns = services.dns("dns")
        self._next_hops = [self.configure_next_hop(ats_factory, index) for index in range(NUM_NEXT_HOPS)]
        self._ats = self.configure_front_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the fixed object set used to exercise the ring."""

        origin = services.origin("server")
        response = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
            "body": "This is the body.\n",
        }
        for index in range(NUM_OBJECTS):
            origin.add_response(
                {"headers": f"GET /obj{index} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
                response,
            )
        return origin

    def configure_next_hop(self, ats_factory: ATSFactory, index: int) -> ATS:
        """Create one parent proxy and DNS record."""

        ats = ats_factory.create(f"ts_nh{index}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        self._dns.add_records({f"next_hop{index}": ["127.0.0.1"]})
        return ats

    def configure_front_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure consistent hashing with the default failover policy."""

        ats = ats_factory.create("ts", return_code=(0, -2))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb",
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.cache.http": 0,
                "proxy.config.http.uncacheable_requests_bypass_parent": 0,
                "proxy.config.http.no_dns_just_forward_to_parent": 1,
                "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
                "proxy.config.http.down_server.cache_time": 1,
                "proxy.config.http.parent_proxy.self_detect": 0,
            })
        lines = ["groups:", "  - &g1"]
        for index, next_hop in enumerate(self._next_hops):
            lines.extend(
                (
                    f"    - host: next_hop{index}",
                    "      protocol:",
                    "        - scheme: http",
                    f"          port: {next_hop.http_port}",
                    "      weight: 1.0",
                ))
        lines.extend(
            (
                "strategies:",
                "  - strategy: the-strategy",
                "    policy: consistent_hash",
                "    hash_key: path",
                "    go_direct: false",
                "    parent_is_proxy: true",
                "    ignore_self_detect: true",
                "    groups:",
                "      - *g1",
                "    scheme: http",
            ))
        ats.write_config_file("strategies.yaml", "\n".join(lines) + "\n")
        ats.remap_config.add_line("map http://dummy.com http://not_used @strategy=the-strategy")
        return ats

    def request_all_objects(self) -> None:
        """Request the complete path set once."""

        for index in range(NUM_OBJECTS):
            result = self._curl.run_for(
                self._ats,
                "--verbose",
                "--proxy",
                f"127.0.0.1:{self._ats.http_port}",
                f"http://dummy.com/obj{index}",
            )
            assert result.returncode == 0, result.output
            assert result.stdout == "This is the body.\n"

    def normalized_trace(self) -> str:
        """Normalize next-hop debug lines exactly as the historical gold did."""

        lines = []
        for line in self._ats.traffic_out.read_text(errors="replace").splitlines():
            if "ParentResultType::SPECIFIED" not in line:
                continue
            line = re.sub(r"^.*\(next_hop\) [^ ]* ", "", line)
            lines.append(re.sub(r"\.\d+$", "", line))
        return "\n".join(lines) + "\n"

    def run(self) -> None:
        """Run the ring before and after restoring the missing parents."""

        self._origin.start()
        self._dns.start()
        for index, next_hop in enumerate(self._next_hops):
            if index not in INITIAL_DOWN:
                next_hop.start()
        self._ats.start()
        self.request_all_objects()
        for index in sorted(INITIAL_DOWN):
            self._next_hops[index].start()
        self.request_all_objects()
        assert self.normalized_trace() == (TEST_DIRECTORY / "trace.gold").read_text()


def test_strategies_ch2(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Consistent hashing walks around unavailable parents and restores the ring."""

    ConsistentHashFailoverScenario(ats_factory, services, curl).run()
