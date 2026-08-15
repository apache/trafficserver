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

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class RateLimitScenario:
    """Exercise rejection, queueing, and independent remap limiters."""

    _drivers = (
        ("concurrent-reject", "concurrent_reject.sh", "fast=429"),
        ("sequential", "sequential_pass.sh", "first=200", "second=200"),
        ("retry-after", "retry_after.sh", "Retry-After: 1"),
        ("queue-drain", "queue_drain.sh", "queued=200"),
        ("independent", "independent_limiters.sh", "independent=200"),
        ("queue-bypass", "queue_bypass_regression.sh", "timing=correct"),
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._clients = self.configure_clients(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Delay responses so concurrent requests overlap at the limiter."""

        origin = services.origin("origin", delay=3)
        for path, body in (("slow", "SLOW"), ("fast", "FAST")):
            origin.add_response(
                {
                    "headers": f"GET /{path} HTTP/1.1\r\nHost: limit.example.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": f"HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n",
                    "body": body,
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure rejecting, queued, and independent remap-plugin instances."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("rate_limit.so"):
            pytest.skip("rate_limit.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rate_limit",
                "proxy.config.http.insert_response_via_str": 0,
                "proxy.config.url_remap.remap_required": 1,
            })
        common = f"http://127.0.0.1:{self._origin.port}/ @plugin=rate_limit.so"
        ats.remap_config.add_line(
            f"map http://limit.example.com/ {common} @pparam=--limit @pparam=1 @pparam=--queue @pparam=0 "
            "@pparam=--error @pparam=429 @pparam=--retry @pparam=1")
        ats.remap_config.add_line(
            f"map http://queued.example.com/ {common} @pparam=--limit @pparam=1 @pparam=--queue @pparam=5 "
            "@pparam=--maxage @pparam=10000 @pparam=--error @pparam=429")
        for hostname in ("limit-a.example.com", "limit-b.example.com"):
            ats.remap_config.add_line(
                f"map http://{hostname}/ {common} @pparam=--limit @pparam=1 @pparam=--queue @pparam=0 "
                "@pparam=--error @pparam=429")
        return ats

    def configure_clients(self, services: ServiceFactory) -> list[tuple[ProcessService, tuple[str, ...]]]:
        """Create the shell drivers and their expected output markers."""

        clients = []
        for name, script, *markers in self._drivers:
            client = services.process(
                name,
                ("/bin/sh", TEST_DIRECTORY / script, str(self._ats.http_port)),
            )
            clients.append((client, tuple(markers)))
        return clients

    def run(self) -> None:
        """Run every limiter behavior against one shared ATS instance."""

        self._origin.start()
        self._ats.start()
        for client, markers in self._clients:
            result = client.run(timeout=30)
            assert result.returncode == 0, result.output
            for marker in markers:
                assert marker in result.stdout, result.output


def test_rate_limit(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The remap limiter rejects, queues, drains, and isolates instances correctly."""

    RateLimitScenario(ats_factory, services).run()
