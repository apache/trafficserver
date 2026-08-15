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
import sys
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class MetricResponse000Scenario:
    """Increment the 000-response metric for an aborted partial request."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl
        self._directory = Path(__file__).parent
        self._origin = self.configure_origin()
        self._ats = self.configure_ats()

    def configure_origin(self) -> OriginServer:
        """Create the origin response used by the successful control request."""

        origin = self._services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self) -> ATS:
        """Configure an uncached reverse proxy for the origin."""

        ats = self._ats_factory.create("ts", enable_cache=False)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
        })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    def abort_partial_request(self) -> None:
        """Send an incomplete request and close the connection."""

        client = self._services.process(
            "abort-client",
            [sys.executable, self._directory / "abort_client.py", "127.0.0.1",
             str(self._ats.http_port)],
        )
        client.run()

    def send_control_request(self) -> None:
        """Verify an ordinary completed transaction still succeeds."""

        result = self._curl.get(self._ats, options=("-s", "-o", "/dev/null", "-w", "%{http_code}"))
        assert result.returncode == 0, result.output
        assert result.stdout == "200"

    def verify_metric(self) -> None:
        """Wait until ATS publishes exactly one 000 response."""

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = self._ats.traffic_ctl("metric", "get", "proxy.process.http.000_responses")
            if result.returncode == 0 and result.stdout.rstrip().endswith(" 1"):
                return
            time.sleep(0.1)
        raise AssertionError(f"000-response metric did not reach one:\n{result.output}")

    def run(self) -> None:
        """Run the aborted request, control request, and metric check."""

        self._origin.start()
        self._ats.start()
        self.abort_partial_request()
        self.send_control_request()
        self.verify_metric()


def test_metric_response_000(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Client aborts increment proxy.process.http.000_responses."""

    MetricResponse000Scenario(ats_factory, services, curl).run()
