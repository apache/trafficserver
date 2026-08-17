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

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, DNSServer, HttpBinServer, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
REPLAY_FILE = TEST_DIRECTORY / "slow_servers.replay.yaml"


class PerServerConnectionMaxScenario:
    """Exercise origin connection limits for replay and CONNECT traffic."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("Connection limit coverage requires TCP client connections")
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    def configure_replay_server(self) -> VerifierServer:
        """Create the delayed verifier origin."""

        return self._services.verifier_server("replay-server", REPLAY_FILE)

    @staticmethod
    def configure_replay_ats(ats_factory: ATSFactory, server: VerifierServer) -> ATS:
        """Configure a three-connection per-port origin limit."""

        ats = ats_factory.create("replay-ts")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|conn_track",
                "proxy.config.http.per_server.connection.max": 3,
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.metric_prefix": "foo",
                "proxy.config.http.per_server.connection.match": "port",
            })
        return ats

    def run_replay_case(self) -> None:
        """Verify a fourth concurrent origin request is tracked as blocked."""

        server = self.configure_replay_server()
        ats = self.configure_replay_ats(self._ats_factory, server)
        client = self._services.verifier_client("replay-client", REPLAY_FILE, http_ports=[ats.http_port])
        server.start()
        ats.start()
        client.run()
        metrics = ats.traffic_ctl("metric", "match", "per_server")
        assert metrics.returncode == 0, metrics.output
        suffix = f"foo.127.0.0.1:{server.http_port}"
        assert f"per_server.total_connection.{suffix} 4" in metrics.stdout
        assert f"per_server.blocked_connection.{suffix} 1" in metrics.stdout
        assert re.search(r"WARNING:.*too many connections:.*limit=3", ats.diags_log.read_text(errors="replace"))

    def configure_connect_services(self, suffix: str) -> tuple[DNSServer, HttpBinServer]:
        """Create DNS and delayed HTTP origin services for a CONNECT case."""

        dns = self._services.dns(f"dns-{suffix}", default="127.0.0.1")
        origin = self._services.httpbin(f"httpbin-{suffix}")
        return dns, origin

    @staticmethod
    def configure_connect_ats(
        ats_factory: ATSFactory,
        suffix: str,
        maximum: int,
        dns: DNSServer,
        origin: HttpBinServer,
    ) -> ATS:
        """Configure a connection limit for tunneled requests."""

        ats = ats_factory.create(f"connect-ts-{suffix}")
        ats.records.update(
            {
                "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|hostdb|conn_track",
                "proxy.config.http.connect_ports": str(origin.port),
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.max": maximum,
            })
        ats.remap_config.add_line(f"map http://foo.com/ http://www.this.origin.com:{origin.port}/")
        ats.allow_private_connect()
        return ats

    def connect_request(self, ats: ATS, path: str) -> CommandResult:
        """Send one proxied request using curl's CONNECT tunnel mode."""

        return self._curl.run_for(
            ats,
            f"--verbose --fail --silent --proxytunnel --proxy '127.0.0.1:{ats.http_port}' 'http://foo.com/{path}'",
        )

    def run_connect_case(self, maximum: int, blocked: int) -> None:
        """Hold three connections while testing two additional requests."""

        suffix = f"max-{maximum}"
        dns, origin = self.configure_connect_services(suffix)
        ats = self.configure_connect_ats(self._ats_factory, suffix, maximum, dns, origin)
        dns.start()
        origin.start()
        ats.start()
        with ThreadPoolExecutor(max_workers=3) as executor:
            slow = [executor.submit(self.connect_request, ats, "delay/2") for _ in range(3)]
            time.sleep(1)
            quick = [self.connect_request(ats, "get") for _ in range(2)]
            slow_results = [future.result(timeout=5) for future in slow]

        for result in slow_results:
            assert result.returncode == 0, result.output
        expected_code = 22 if blocked else 0
        expected_status = "503" if blocked else "200"
        for result in quick:
            assert result.returncode == expected_code, result.output
            assert f"HTTP/1.1 {expected_status}" in result.stderr

        metrics = ats.traffic_ctl("metric", "match", "per_server")
        assert metrics.returncode == 0, metrics.output
        suffix = f"www.this.origin.com.127.0.0.1:{origin.port}"
        assert f"per_server.total_connection.{suffix} 5" in metrics.stdout
        assert f"per_server.blocked_connection.{suffix} {blocked}" in metrics.stdout

    def run(self) -> None:
        """Run ordinary origin and CONNECT connection-limit cases."""

        self.run_replay_case()
        self.run_connect_case(3, 2)
        self.run_connect_case(0, 0)


def test_per_server_connection_max(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS enforces and reports per-origin connection limits."""

    PerServerConnectionMaxScenario(ats_factory, services, curl).run()
