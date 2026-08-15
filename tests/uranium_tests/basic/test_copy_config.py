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


class ExplicitListenerScenario:
    """Run two ATS instances with independently configured listeners."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self.ats_factory = ats_factory
        self.curl = curl

    def _configure_traffic_servers(self) -> None:
        self.first = self.ats_factory.create("ts1")
        self.first.records.update({
            "proxy.config.http.server_ports": f"{self.first.http_port} {self.first.uds_path}",
        })
        self.second = self.ats_factory.create("ts2")
        self.second.records.update({"proxy.config.http.server_ports": str(self.second.http_port)})

    def _start_traffic_servers(self) -> None:
        self.first.start()
        self.second.start()

    def _request_each_instance(self) -> None:
        first = self.curl.get(self.first)
        second = self.curl.run(f"http://127.0.0.1:{self.second.http_port}/")

        assert first.returncode == 0, first.output
        assert second.returncode == 0, second.output

    def run(self) -> None:
        self._configure_traffic_servers()
        self._start_traffic_servers()
        self._request_each_instance()


def test_explicit_listener_configuration(ats_factory: ATSFactory, curl: Curl) -> None:
    ExplicitListenerScenario(ats_factory, curl).run()
