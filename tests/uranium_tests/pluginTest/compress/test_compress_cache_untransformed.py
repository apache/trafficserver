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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class CompressCacheUntransformedScenario:
    """Exercise a cached compress transform after an origin 100 response."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the origin that sends 100 Continue before its final response."""

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "compress_100_continue_origin.py", "--port", str(self._origin_port)),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable POST caching and untransformed cache writes in compress.so."""

        ats = ats_factory.create("ts", enable_cache=True)
        if not ats.plugin_exists("compress.so"):
            pytest.skip("compress.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|compress|http_tunnel",
                "proxy.config.http.send_100_continue_response": 0,
                "proxy.config.http.cache.post_method": 1,
            })
        config = TEST_DIRECTORY / "etc" / "compress-cache-false.config"
        ats.copy_to_config(config)
        ats.remap_config.add_line(
            f"map / http://127.0.0.1:{self._origin_port}/ @plugin=compress.so "
            f"@pparam={ats.config_directory / config.name}")
        return ats

    def run(self) -> None:
        """Send the triggering POST and require ATS to survive it."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            (
                f"--http1.1 --silent --output /dev/null --request POST --header 'Accept-Encoding: gzip' --header "
                f"'Expect: 100-continue' --expect100-timeout 0 --data 'test body data' "
                f"'http://127.0.0.1:{self._ats.http_port}/test/resource.js'"),
        )
        assert result.returncode == 0, result.output
        assert self._ats.is_running


def test_compress_cache_untransformed(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A 100 response must not leave stale header bytes in the cache tunnel."""

    CompressCacheUntransformedScenario(ats_factory, services, curl).run()
