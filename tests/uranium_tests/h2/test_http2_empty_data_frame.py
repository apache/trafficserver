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

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ServiceFactory


class EmptyDataFrameScenario:
    """Exercise empty end-of-stream DATA frames on one HTTP/2 connection."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._client = Path(__file__).parent / "clients" / "h2empty_data_frame.py"
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> HttpBinServer:
        """Create an origin that serves the cacheable response."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure HTTP/2 error-rate accounting and the origin remap."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2",
                "proxy.config.http.insert_response_via_str": 2,
                "proxy.config.http2.active_timeout_in": 3,
                "proxy.config.http2.stream_error_rate_threshold": 0.1,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def run_client(self, name: str, streams: int) -> None:
        """Send empty DATA frames on @a streams streams."""

        result = self._services.process(
            name,
            [sys.executable, self._client, str(self._ats.https_port), "/cache/10", "-n",
             str(streams)],
        ).run()
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Prime the cache, then verify twenty streams do not trip the threshold."""

        self._origin.start()
        self._ats.start()
        self.run_client("warm-cache", 1)
        self.run_client("twenty-streams", 20)
        assert self._ats.is_running


def test_http2_empty_data_frame(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Empty end-of-stream DATA frames are not counted as stream errors."""

    EmptyDataFrameScenario(ats_factory, services).run()
