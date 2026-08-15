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
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class SlowOriginProbeScenario:
    """Verify the full-JSON transform makes progress across delayed body chunks."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._ats = self.configure_ats(ats_factory)
        self._origin_ready = self._ats.run_directory / "origin.ready"
        self._server = self.configure_server(services)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable detailed transform logging and bounded transaction timeouts."""

        ats = ats_factory.create("ts", enable_cache=False)
        if not ats.plugin_exists("xdebug.so"):
            pytest.skip("xdebug.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "xdebug_transform",
                "proxy.config.http.transaction_no_activity_timeout_in": 10,
                "proxy.config.http.transaction_no_activity_timeout_out": 10,
            })
        ats.plugin_config.add_line("xdebug.so --enable=probe-full-json")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin_port}/")
        return ats

    def configure_server(self, services: ServiceFactory) -> ProcessService:
        """Run the netcat origin that pauses between its two chunks."""

        if shutil.which("nc") is None:
            pytest.skip("nc is required")
        request_path = self._ats.run_directory / "server_request.txt"
        return services.process(
            "slow-origin",
            (
                "bash",
                str(TEST_DIRECTORY / "slow-body-server.sh"),
                str(self._origin_port),
                str(request_path),
                str(self._origin_ready),
            ),
        )

    def wait_for_origin(self) -> None:
        """Wait until the server has opened both ends of its response pipe."""

        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self._origin_ready.exists():
                return
            time.sleep(0.05)
        raise AssertionError("The slow origin did not open its listener")

    def request(self) -> None:
        """Require the transformed request to complete before curl's hang detector."""

        result = self._curl.get(
            self._ats,
            "/test",
            headers={
                "Host": "example.com",
                "X-Debug": "probe-full-json=nobody"
            },
            options=("--silent", "--output", "/dev/null", "--write-out", "%{http_code}", "--max-time", "8"),
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert result.stdout == "200"

    def verify_transform_progress(self) -> None:
        """Bound empty callbacks and require both body chunks to be consumed."""

        trace = self._ats.traffic_out.read_text(errors="replace")
        expected_count = trace.count("bytes of body is expected")
        consumed_count = len([line for line in trace.splitlines() if "consumed" in line and "bytes" in line])
        assert expected_count <= 10, f"The transform appears to be looping ({expected_count} callbacks)\n{trace}"
        assert consumed_count == 2, f"Expected both delayed chunks to be consumed, found {consumed_count}\n{trace}"

    def run(self) -> None:
        """Start ATS and the bespoke origin, then inspect the completed transform."""

        self._ats.start()
        self._server.start()
        self.wait_for_origin()
        self.request()
        self._server.stop()
        self.verify_transform_progress()


def test_x_probe_full_json_slow_origin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A delayed chunked origin cannot send the full-JSON transform into a callback loop."""

    SlowOriginProbeScenario(ats_factory, services, curl).run()
