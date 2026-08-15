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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class SlowOriginPostScenario:
    """Exercise a POST while the origin stalls its TLS handshake."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._ready_file = ats_factory.run_directory / "origin.ready"
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the one-shot clear-text netcat server used as a TLS origin."""

        return services.process(
            "origin",
            ("bash", TEST_DIRECTORY / "server.sh", str(self._origin_port), self._ready_file),
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Set the outbound connect timeout and inbound request limit."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.net.max_requests_in": 1000,
                "proxy.config.http.connect_attempts_timeout": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|socket|v_net_queue",
            })
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._origin_port}/")
        return ats

    def wait_for_origin(self) -> None:
        """Wait for the server script to reach its netcat listener."""

        deadline = time.monotonic() + 10
        while not self._ready_file.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert self._ready_file.exists(), self._origin.output

    def run(self) -> None:
        """Send the POST and compare the generated 502 response."""

        self._origin.start()
        self.wait_for_origin()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            "--request",
            "POST",
            "--http1.1",
            "--verbose",
            "--silent",
            f"http://127.0.0.1:{self._ats.http_port}/",
            "--data",
            "key=value",
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stdout, TEST_DIRECTORY / "gold" / "post_slow_server_max_requests_in_0_stdout.gold")
        assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / "post_slow_server_max_requests_in_0_stderr.gold")


def test_post_slow_server_max_requests_in(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A stalled origin handshake does not trigger the inbound request limit."""

    SlowOriginPostScenario(ats_factory, services, curl).run()
