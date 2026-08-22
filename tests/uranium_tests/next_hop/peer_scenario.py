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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, assert_matches_gold


class PeerStrategyScenario:
    """Run the consistent-hash peering topology with or without an upstream group."""

    NUM_OBJECTS = 16
    NUM_PEERS = 8
    NUM_UPSTREAMS = 6

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        test_directory: Path,
        *,
        has_upstream_group: bool,
    ) -> None:
        self._curl = curl
        self._test_directory = test_directory
        self._has_upstream_group = has_upstream_group
        self._origin = self.configure_origin(services)
        self._dns = services.dns("dns")
        self._upstreams = [ats_factory.create(f"ts_upstream{index}") for index in range(self.NUM_UPSTREAMS)]
        self._peers = [ats_factory.create(f"ts_peer{index}", capture_traffic_out=False) for index in range(self.NUM_PEERS)]
        self.configure_dns()
        self.configure_upstreams()
        self.configure_peers()

    @classmethod
    def configure_origin(cls, services: ServiceFactory) -> OriginServer:
        """Create the common cacheable origin objects."""

        origin = services.origin("server")
        for index in range(cls.NUM_OBJECTS):
            origin.add_response(
                {"headers": f"GET /obj{index} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
                {
                    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                    "body": "This is the body.\n",
                },
            )
        return origin

    def configure_dns(self) -> None:
        """Resolve all ATS peer and upstream names to loopback."""

        records = {
            **{
                f"ts_peer{index}": ["127.0.0.1"] for index in range(self.NUM_PEERS)
            },
            **{
                f"ts_upstream{index}": ["127.0.0.1"] for index in range(self.NUM_UPSTREAMS)
            },
        }
        self._dns.add_records(records)

    def common_records(self) -> dict[str, object]:
        """Return records shared by every ATS instance in the topology."""

        return {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        }

    def configure_upstreams(self) -> None:
        """Map each upstream ATS directly to the microserver."""

        for upstream in self._upstreams:
            upstream.records.update({**self.common_records(), "proxy.config.diags.debug.tags": "http|dns"})
            upstream.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")

    @staticmethod
    def group_lines(anchor: str, hosts: list[ATS]) -> list[str]:
        """Render one strategies.yaml next-hop group."""

        lines = [f"  - &{anchor}"]
        for index, host in enumerate(hosts):
            lines.extend(
                (
                    f"    - host: {host.name}",
                    "      protocol:",
                    "        - scheme: http",
                    f"          port: {host.http_port}",
                    "      weight: 1.0",
                ))
        return lines

    def strategies_document(self, peer_index: int) -> str:
        """Render the peering strategy for one peer's self identity."""

        lines = ["groups:", *self.group_lines("peer_group", self._peers)]
        if self._has_upstream_group:
            lines.extend(self.group_lines("peer_upstream", self._upstreams))
        lines.extend(
            (
                "strategies:",
                "  - strategy: the-strategy",
                "    policy: consistent_hash",
                f"    hash_key: {'cache_key' if self._has_upstream_group else 'path'}",
                f"    go_direct: {'false' if self._has_upstream_group else 'true'}",
                "    parent_is_proxy: true",
                "    cache_peer_result: false",
                "    ignore_self_detect: false",
                "    groups:",
                "      - *peer_group",
            ))
        if self._has_upstream_group:
            lines.append("      - *peer_upstream")
        lines.extend((
            "    scheme: http",
            "    failover:",
            "      ring_mode: peering_ring",
            f"      self: ts_peer{peer_index}",
        ))
        return "\n".join(lines) + "\n"

    def configure_peers(self) -> None:
        """Configure all cache peers with their self-specific strategy."""

        tags = "http|dns|parent|next_hop|host_statuses|hostdb"
        if self._has_upstream_group:
            tags += "|cachekey"
        for index, peer in enumerate(self._peers):
            peer.records.update(
                {
                    **self.common_records(),
                    "proxy.config.diags.debug.tags": tags,
                    "proxy.config.http.cache.http": 1,
                    "proxy.config.http.cache.required_headers": 0,
                    "proxy.config.http.uncacheable_requests_bypass_parent": 0,
                    "proxy.config.http.no_dns_just_forward_to_parent": int(self._has_upstream_group),
                    "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
                    "proxy.config.http.parent_proxy.self_detect": 1,
                })
            peer.write_config_file("strategies.yaml", self.strategies_document(index))
            if self._has_upstream_group:
                suffix = (
                    " @strategy=the-strategy @plugin=cachekey.so"
                    " @pparam=--uri-type=remap @pparam=--capture-prefix=/(.*):(.*)/$1/")
                peer.remap_config.add_lines(
                    ("map http://dummy.com http://not_used" + suffix, "map http://not_used http://also_not_used" + suffix))
            else:
                for upstream in self._upstreams:
                    prefix = f"http://{upstream.name}:{upstream.http_port}/"
                    peer.remap_config.add_line(f"map {prefix} {prefix} @strategy=the-strategy")

    def request_url(self, object_index: int) -> str:
        """Return the client URL for this scenario variant."""

        if self._has_upstream_group:
            return f"http://dummy.com/obj{object_index}"
        upstream = self._upstreams[0]
        return f"http://{upstream.name}:{upstream.http_port}/obj{object_index}"

    def request(self, peer_index: int, object_index: int) -> None:
        """Fetch one object through the selected cache peer."""

        peer = self._peers[peer_index]
        result = self._curl.run_for(
            peer,
            f"--verbose --proxy '127.0.0.1:{peer.http_port}' '{self.request_url(object_index)}'",
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "This is the body.\n"

    def normalized_trace(self) -> str:
        """Extract and normalize the same trace lines as the historical gold files."""

        selected = re.compile(r"^(?:\+\+\+|[A-Z].*TTP/|\[alts\] --)|ParentResultType::SPECIFIED")
        output = []
        for peer_index, peer in enumerate(self._peers):
            for line in peer.process_output.splitlines():
                if selected.search(line) is None:
                    continue
                if "(next_hop)" in line:
                    line = re.sub(r"^.*\(next_hop\) \S+ ", "", line)
                    line = re.sub(r"\.\d+$", "", line)
                else:
                    line = f"trace_peer{peer_index}.log:{line}"
                output.append(line)
        trace = "\n".join(output) + "\n"
        if not self._has_upstream_group:
            for index, upstream in enumerate(self._upstreams):
                trace = trace.replace(f":{upstream.http_port}", f":UP_PORT{index}")
        return trace

    def run(self) -> None:
        """Start the topology, exercise both ingress patterns, and validate traces."""

        self._origin.start()
        self._dns.start()
        for upstream in self._upstreams:
            upstream.start()
        for peer in self._peers:
            peer.start()
        for object_index in range(self.NUM_OBJECTS):
            self.request(object_index % self.NUM_PEERS, object_index)
        for object_index in range(self.NUM_OBJECTS):
            self.request((object_index * 3) % self.NUM_PEERS, object_index)
        assert_matches_gold(self.normalized_trace(), self._test_directory / "trace.gold")
