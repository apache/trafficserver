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

from tools.uranium.services import ATS, Curl


class CustomListenerScenario:
    """Verify that records.yaml can replace the default listener configuration."""

    def __init__(self, ats: ATS, curl: Curl) -> None:
        self.ats = ats
        self.curl = curl

    def _configure_traffic_server(self) -> None:
        self.ats.records.update({"proxy.config.http.server_ports": f"{self.ats.http_port} {self.ats.uds_path}"})

    def _start_traffic_server(self) -> None:
        self.ats.start()

    def _verify_custom_listener_accepts_requests(self) -> None:
        result = self.curl.get(self.ats)

        assert result.returncode == 0, result.output

    def run(self) -> None:
        self._configure_traffic_server()
        self._start_traffic_server()
        self._verify_custom_listener_accepts_requests()


def test_traffic_server_starts_with_custom_listener(ats: ATS, curl: Curl) -> None:
    CustomListenerScenario(ats, curl).run()
