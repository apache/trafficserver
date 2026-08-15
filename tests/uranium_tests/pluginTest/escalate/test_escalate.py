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

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent


class EscalateScenario:
    """Route failed origin requests through the escalate failover plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        probe = ats_factory.create("plugin-probe")
        if not probe.plugin_exists("escalate.so"):
            pytest.skip("escalate.so is not installed")

    def configure_dns(self, suffix: str) -> DNSServer:
        """Resolve every logical origin name to loopback."""

        return self._services.dns(f"dns-{suffix}", default="127.0.0.1")

    def configure_servers(
        self,
        suffix: str,
        origin_replay: str,
        failover_replay: str,
    ) -> tuple[VerifierServer, VerifierServer]:
        """Create the primary and failover verifier origins."""

        origin = self._services.verifier_server(f"origin-{suffix}", TEST_DIRECTORY / origin_replay)
        failover = self._services.verifier_server(f"failover-{suffix}", TEST_DIRECTORY / failover_replay)
        return origin, failover

    def configure_ats(
        self,
        suffix: str,
        dns: DNSServer,
        origin: VerifierServer,
        failover: VerifierServer,
        *,
        disable_redirect_header: bool,
        enable_cache: bool,
        escalate_non_get: bool,
    ) -> ATS:
        """Configure primary and down-origin mappings for one option set."""

        ats = self._ats_factory.create(f"ts-{suffix}", enable_cache=enable_cache)
        dead_port = self._services.allocate_port()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|escalate",
                "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.redirect.actions": "self:follow",
                "proxy.config.http.number_of_redirections": 4,
            })
        options = []
        if disable_redirect_header:
            options.append("@pparam=--no-redirect-header")
        if escalate_non_get:
            options.append("@pparam=--escalate-non-get-methods")
        parameters = " ".join(options)
        plugin = (f"@plugin=escalate.so @pparam=500,502:failover.server.com:{failover.http_port} {parameters}")
        ats.remap_config.add_lines(
            (
                f"map http://origin.server.com http://backend.origin.server.com:{origin.http_port} {plugin}",
                f"map http://down_origin.server.com http://backend.down_origin.server.com:{dead_port} {plugin}",
            ))
        return ats

    def configure_client(self, suffix: str, replay: str, ats: ATS) -> ProcessService:
        """Create the verifier client that drives one option set."""

        return self._services.verifier_client(
            f"client-{suffix}",
            TEST_DIRECTORY / replay,
            http_ports=[ats.http_port],
        )

    @staticmethod
    def assert_standard_outputs(
        origin: VerifierServer,
        failover: VerifierServer,
        client: ProcessService,
        *,
        disable_redirect_header: bool,
    ) -> None:
        """Verify the default GET-only escalation behavior."""

        origin_output = origin.output
        failover_output = failover.output
        client_output = client.output
        for key in ("GET", "GET_chunked", "GET_failed", "HEAD_fail_not_escalated", "POST_fail_not_escalated"):
            assert f"uuid: {key}" in origin_output
        assert "uuid: GET_down_origin" not in origin_output
        assert "x-escalate-redirect" not in origin_output
        for key in ("GET_failed", "GET_down_origin"):
            assert f"uuid: {key}" in failover_output
        for key in ("GET_chunked", "HEAD_fail_not_escalated", "POST_fail_not_escalated"):
            assert f"uuid: {key}" not in failover_output
        if disable_redirect_header:
            assert "x-escalate-redirect" not in failover_output
        else:
            assert "x-escalate-redirect: 1" in failover_output
        for response in ("first", "second", "third", "fourth", "head_fail_not_escalated", "post_fail_not_escalated"):
            assert f"x-response: {response}" in client_output
        assert "502 Bad Gateway" in client_output
        assert "[ERROR]" not in client_output

    @staticmethod
    def assert_non_get_outputs(origin: VerifierServer, failover: VerifierServer, client: ProcessService) -> None:
        """Verify explicitly enabled non-GET escalation behavior."""

        origin_output = origin.output
        failover_output = failover.output
        client_output = client.output
        for key in ("GET", "GET_chunked", "GET_failed", "POST_success", "HEAD_fail_escalated"):
            assert f"uuid: {key}" in origin_output
        assert "uuid: GET_down_origin" not in origin_output
        for key in ("GET_failed", "GET_down_origin", "HEAD_fail_escalated"):
            assert f"uuid: {key}" in failover_output
        assert "uuid: POST_success" not in failover_output
        for response in ("first", "second", "third", "fourth", "post_success", "head_fail_escalated"):
            assert f"x-response: {response}" in client_output
        assert "502 Bad Gateway" not in client_output
        assert "[ERROR]" not in client_output

    def run_case(
        self,
        suffix: str,
        *,
        disable_redirect_header: bool = False,
        enable_cache: bool = False,
        escalate_non_get: bool = False,
    ) -> None:
        """Create, run, and validate one escalate option set."""

        if escalate_non_get:
            client_replay = "escalate_non_get_methods.replay.yaml"
            origin_replay = "escalate_original_server_non_get.replay.yaml"
            failover_replay = "escalate_failover_server_non_get.replay.yaml"
        else:
            client_replay = "escalate_original.replay.yaml"
            origin_replay = "escalate_original_server_default.replay.yaml"
            failover_replay = "escalate_failover_server_default.replay.yaml"

        dns = self.configure_dns(suffix)
        origin, failover = self.configure_servers(suffix, origin_replay, failover_replay)
        ats = self.configure_ats(
            suffix,
            dns,
            origin,
            failover,
            disable_redirect_header=disable_redirect_header,
            enable_cache=enable_cache,
            escalate_non_get=escalate_non_get,
        )
        client = self.configure_client(suffix, client_replay, ats)
        dns.start()
        origin.start()
        failover.start()
        ats.start()
        client.run()
        if escalate_non_get:
            self.assert_non_get_outputs(origin, failover, client)
        else:
            self.assert_standard_outputs(
                origin,
                failover,
                client,
                disable_redirect_header=disable_redirect_header,
            )

    def run(self) -> None:
        """Exercise header, cache, and non-GET plugin options."""

        self.run_case("default")
        self.run_case("no-header", disable_redirect_header=True)
        self.run_case("cached", enable_cache=True)
        self.run_case("non-get", escalate_non_get=True)


def test_escalate(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """escalate fails over selected responses and optionally non-GET methods."""

    EscalateScenario(ats_factory, services).run()
