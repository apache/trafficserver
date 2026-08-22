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

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ServiceFactory, wait_for_file_lines

NUM_PARENTS = 100


class ParentDownRingWalkScenario:
    """Select from an all-down consistent-hash pool larger than MAX_PARENTS."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._hostnames = [f"deadparent{index:03d}" for index in range(1, NUM_PARENTS + 1)]
        self._ports = [services.allocate_port() for _ in self._hostnames]
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the parent pool and parent-selection diagnostics."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "parent_select",
                "proxy.config.http.no_dns_just_forward_to_parent": 1,
                "proxy.config.http.parent_proxy.fail_threshold": 10,
                "proxy.config.http.parent_proxy.retry_time": 300,
                "proxy.config.http.parent_proxy.self_detect": 0,
                "proxy.config.url_remap.remap_required": 0,
            })
        parent_list = ", ".join(f"{hostname}:{port}|1" for hostname, port in zip(self._hostnames, self._ports, strict=True))
        ats.parent_config.add_line(
            f'dest_domain=. parent="{parent_list}" round_robin=consistent_hash go_direct=false parent_is_proxy=true')
        return ats

    def mark_parents_down(self) -> None:
        """Populate HostStatus for the complete pool in one RPC call."""

        result = self._ats.traffic_ctl("host", "down", *self._hostnames)
        assert result.returncode == 0, result.output

    @staticmethod
    def verify_response(result: CommandResult) -> None:
        """Require the all-down pool to generate a 502 response."""

        assert result.returncode == 0, result.output
        assert result.stdout == "502"

    def run(self) -> None:
        """Mark the pool down, issue one request, and inspect lock-read accounting."""

        self._ats.start()
        self.mark_parents_down()
        result = self._curl.run_for(
            self._ats,
            (
                f"--silent --output /dev/null --write-out '%{{http_code}}' --proxy '127.0.0.1:{self._ats.http_port}' "
                f"http://example.com/ring-walk-probe"),
        )
        self.verify_response(result)
        traffic_out = wait_for_file_lines(self._ats.traffic_out, rf"getHostStatus calls: {NUM_PARENTS}\b", 1)
        assert re.search(r"getHostStatus calls: [0-9]{5}", traffic_out) is None, traffic_out


def test_consistent_hash_ring_walk(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An all-down hash pool reads each distinct parent's HostStatus once."""

    ParentDownRingWalkScenario(ats_factory, services, curl).run()
