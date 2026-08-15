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

from tools.uranium.services import ATSFactory, Curl


class ConcurrentTrafficServersScenario:
    """Verify that two independently configured Traffic Servers can run together."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl

    def _configure_traffic_servers(self) -> None:
        self.servers = [self.ats_factory.create("ts1"), self.ats_factory.create("ts2")]

    def _start_traffic_servers(self) -> None:
        for server in self.servers:
            server.start()

    def _verify_traffic_servers_accept_requests(self) -> None:
        for server in self.servers:
            result = self.curl.get(server)

            assert result.returncode == 0, result.output
            assert all(instance.is_running for instance in self.servers)

    def run(self) -> None:
        self._configure_traffic_servers()
        self._start_traffic_servers()
        self._verify_traffic_servers_accept_requests()


def test_multiple_traffic_servers_run_concurrently(ats_factory: ATSFactory, curl: Curl) -> None:
    ConcurrentTrafficServersScenario(ats_factory, curl).run()
