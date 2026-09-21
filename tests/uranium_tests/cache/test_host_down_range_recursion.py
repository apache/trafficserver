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

from tools.uranium.services import ATS, ServiceFactory
from uranium_tests.lib.jsonrpc import Request


class HostDownRangeRecursionScenario:
    """An unsatisfied Range request to a DOWN host does not recurse forever."""

    PRIME_REPLAY = "replay/host_down_range_recursion_prime.replay.yaml"
    RANGE_REPLAY = "replay/host_down_range_recursion_range.replay.yaml"

    def __init__(self, ats: ATS, services: ServiceFactory) -> None:
        self.ats = ats
        self.services = services

    def _configure_origin(self) -> None:
        self.origin = self.services.verifier_server("server", self.PRIME_REPLAY)

    def _configure_traffic_server(self) -> None:
        self.ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|host_statuses",
                "proxy.config.http.cache.range.write": 1,
                "proxy.config.http.insert_response_via_str": 3,
            })
        self.ats.remap_config.add_line(f"map http://backend.example.com/ http://127.0.0.1:{self.origin.http_port}/")

    def _start_services(self) -> None:
        self.origin.start()
        self.ats.start()

    def _prime_cache(self) -> None:
        result = self.services.verifier_client("prime-client", self.PRIME_REPLAY, http_ports=[self.ats.http_port]).run()

        assert result.returncode == 0, result.output

    def _mark_origin_down(self) -> None:
        result = self.ats.rpc(Request.admin_host_set_status(
            operation="down",
            host=["127.0.0.1"],
            reason="manual",
            time="0",
        ))

        assert result.returncode == 0, result.output

    def _request_unsatisfied_range(self) -> None:
        result = self.services.verifier_client("range-client", self.RANGE_REPLAY, http_ports=[self.ats.http_port]).run(timeout=10)

        assert result.returncode == 0, result.output

    def _verify_services_survived(self) -> None:
        assert self.ats.is_running
        assert self.origin.is_running

    def run(self) -> None:
        self._configure_origin()
        self._configure_traffic_server()
        self._start_services()
        self._prime_cache()
        self._mark_origin_down()
        self._request_unsatisfied_range()
        self._verify_services_survived()


def test_host_down_range_recursion(ats: ATS, services: ServiceFactory) -> None:
    HostDownRangeRecursionScenario(ats, services).run()
