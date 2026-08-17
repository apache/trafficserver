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
import os
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class CacheDirectoryProbeScenario:
    """Trace cache directory insert and remove USDT probes with bpftrace."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Configure the cache-probe scenario.

        :param ats_factory: Factory that owns the ATS instance.
        :param services: Factory that owns the origin and tracer processes.
        :param curl: Curl client used for cache operations.
        """

        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._tracer = self.configure_tracer(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve a cacheable object and accept its PURGE request.

        :param services: Factory that owns the origin process.
        """

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /cacheable HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
                "body": ""
            },
            {
                "headers": ("HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=120\r\n"
                            "Content-Length: 5\r\n\r\n"),
                "body": "hello",
            },
        )
        origin.add_response(
            {
                "headers": "PURGE /cacheable HTTP/1.1\r\nHost: cache-probe.test\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Keep ATS privileged so bpftrace can observe its cache probes.

        :param ats_factory: Factory that owns the ATS instance.
        """

        ats = ats_factory.create("ts", enable_cache=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|cache",
                "proxy.config.http.cache.required_headers": 0,
                "proxy.config.admin.user_id": "#-1",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    @staticmethod
    def configure_tracer(services: ServiceFactory) -> ProcessService:
        """Create the bpftrace process for the cache probe script.

        :param services: Factory that owns the tracer process.
        """

        return services.process("bpftrace", ("bpftrace", TEST_DIRECTORY / "cache_dir_probe.bt"))

    def wait_for_trace(self, expression: str, timeout: float = 10) -> None:
        """Wait for one marker in the live tracer output.

        :param expression: Marker to find in the accumulated bpftrace output.
        :param timeout: Maximum number of seconds to wait for the marker.
        """

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if expression in self._tracer.output:
                return
            if not self._tracer.is_running and self._tracer.output:
                pytest.skip(f"bpftrace cannot attach to the ATS probes:\n{self._tracer.output}")
            time.sleep(0.1)
        raise AssertionError(f"Expected {expression!r} in bpftrace output:\n{self._tracer.output}")

    def request(self, method: str = "GET") -> None:
        """Send one cache operation through ATS.

        :param method: HTTP method for the cache operation.
        """

        result = self._curl.run_for(
            self._ats,
            f"--silent --show-error --fail --output /dev/null --request {method} "
            f"--header 'Host: cache-probe.test' http://127.0.0.1:{self._ats.http_port}/cacheable",
        )
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Fill, purge, and refill while tracing both directory operations."""

        if os.geteuid() != 0 or shutil.which("bpftrace") is None:
            pytest.skip("cache probe tracing requires root and bpftrace")
        self._origin.start()
        self._ats.start()
        self._tracer.start()
        self.wait_for_trace("cache_dir_probe: ready")
        self.request()
        self.request("PURGE")
        self.request()
        self.wait_for_trace("cache_dir_insert")
        self.wait_for_trace("cache_dir_remove")


def test_cache_dir_probe(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Cache fill and PURGE fire their directory USDT probes.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the origin and tracer processes.
    :param curl: Curl client used for cache operations.
    """

    CacheDirectoryProbeScenario(ats_factory, services, curl).run()
