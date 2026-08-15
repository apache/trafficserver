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

from tools.uranium.services import ATS, ATSFactory, Curl, HttpBinServer, ServiceFactory, wait_for_file_lines


class SimplePostBufferScenario:
    """Exercise request buffering across a 100-continue exchange."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> HttpBinServer:
        """Create the HTTPBin POST endpoint."""

        return services.httpbin("origin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable request buffering and HTTP debug diagnostics."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.request_buffer_enabled": 1,
                "proxy.config.http.number_of_redirections": 1,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def run(self) -> None:
        """POST through ATS and verify both response milestones were logged."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--header",
            "Expect: 100-continue",
            "--data",
            "abc",
            f"http://127.0.0.1:{self._ats.http_port}/post",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200 OK" in result.stderr, result.output
        output = wait_for_file_lines(self._ats.traffic_out, r"HTTP/1\.1 100 Continue", 1)
        assert "HTTP/1.1 200 OK" in output


def test_simple_post_valid_buffer_check(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A buffered 100-continue POST does not leave ATS without a write buffer."""

    SimplePostBufferScenario(ats_factory, services, curl).run()
