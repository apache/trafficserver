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
STAT_SYNC_INTERVAL_MS = 500


class PerServerConnectionMaxScenario:
    """Exercise origin limits and every per-server metric publication mode."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Create the shared services and retain the scenario fixtures.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param services: Factory for DNS, origin, and verifier services.
        :param curl: Transport-aware curl command runner.
        """

        if curl.uses_uds:
            pytest.skip("Connection limit coverage requires TCP client connections")
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl
        self._dns = self.configure_dns()

    def configure_dns(self) -> DNSServer:
        """Create the wildcard DNS server shared by all scenario steps."""

        return self._services.dns("dns", default="127.0.0.1")

    def configure_common_records(self, ats: ATS) -> None:
        """Configure DNS and a short derived-metric synchronization interval.

        :param ats: Traffic Server instance receiving the shared records.
        """

        ats.records.update(
            {
                "proxy.config.raw_stat_sync_interval_ms": STAT_SYNC_INTERVAL_MS,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })

    @staticmethod
    def read_metrics(ats: ATS, *, include_hidden: bool = False) -> str:
        """Read the per-server metric namespace.

        :param ats: Traffic Server instance queried with ``traffic_ctl``.
        :param include_hidden: Whether to include internal per-group metrics.
        """

        arguments = ["metric", "match", "per_server"]
        if include_hidden:
            arguments.append("--include-hidden")
        result = ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        assert "INVALID_INCOMING_DATA" not in result.output
        return result.output

    def wait_for_metrics(
        self,
        ats: ATS,
        required: tuple[str, ...],
        *,
        include_hidden: bool = False,
        timeout: float = 10,
    ) -> str:
        """Wait until one metric query contains every required line fragment.

        :param ats: Traffic Server instance queried with ``traffic_ctl``.
        :param required: Text fragments that must all appear in one response.
        :param include_hidden: Whether to include internal per-group metrics.
        :param timeout: Maximum number of seconds to wait for a derived-metric
            synchronization tick.
        """

        deadline = time.monotonic() + timeout
        last_output = ""
        while time.monotonic() < deadline:
            last_output = self.read_metrics(ats, include_hidden=include_hidden)
            if all(fragment in last_output for fragment in required):
                return last_output
            time.sleep(0.1)
        pytest.fail(f"Timed out waiting for per-server metrics {required!r}:\n{last_output}")

    def configure_replay_server(self) -> VerifierServer:
        """Create the delayed verifier origin."""

        return self._services.verifier_server("replay-server", REPLAY_FILE)

    def configure_replay_ats(self, server: VerifierServer) -> ATS:
        """Configure a three-connection per-port origin limit.

        :param server: Delayed verifier origin used by the replay.
        """

        ats = self._ats_factory.create("replay-ts")
        self.configure_common_records(ats)
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
        ats = self.configure_replay_ats(server)
        client = self._services.verifier_client("replay-client", REPLAY_FILE, http_ports=[ats.http_port])
        server.start()
        ats.start()
        result = client.run()
        assert result.returncode == 0, result.output

        group = f"foo.127.0.0.1:{server.http_port}"
        metrics = self.wait_for_metrics(
            ats,
            (
                f"per_server.total_connection.{group} 4",
                f"per_server.blocked_connection.{group} 1",
            ),
        )
        assert "per_server.current_connection_max." not in metrics
        assert re.search(r"WARNING:.*too many connections:.*limit=3", ats.diags_log.read_text(errors="replace"))

    def configure_connect_ats(
        self,
        suffix: str,
        maximum: int,
        metric_aggregate: int,
        origin: HttpBinServer,
    ) -> ATS:
        """Configure a connection limit for tunneled requests.

        :param suffix: Unique process-name suffix for this case.
        :param maximum: Maximum simultaneous connections, or zero for no limit.
        :param metric_aggregate: Per-server metric publication level.
        :param origin: Delayed HTTP origin reached through CONNECT.
        """

        ats = self._ats_factory.create(f"connect-ts-{suffix}")
        self.configure_common_records(ats)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|hostdb|conn_track",
                "proxy.config.http.connect_ports": str(origin.port),
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.metric_aggregate": metric_aggregate,
                "proxy.config.http.per_server.connection.max": maximum,
            })
        ats.remap_config.add_line(f"map http://foo.com/ http://www.this.origin.com:{origin.port}/")
        ats.allow_private_connect()
        return ats

    def connect_request(self, ats: ATS, path: str) -> CommandResult:
        """Send one proxied request using curl's CONNECT tunnel mode.

        :param ats: Traffic Server proxy that receives the request.
        :param path: Request path below ``http://foo.com/``.
        """

        return self._curl.run_for(
            ats,
            f"--verbose --fail --silent --proxytunnel --proxy '127.0.0.1:{ats.http_port}' 'http://foo.com/{path}'",
        )

    def run_connect_case(self, maximum: int, blocked: int, metric_aggregate: int) -> None:
        """Hold three connections while testing aggregate publication.

        :param maximum: Maximum simultaneous connections, or zero for no limit.
        :param blocked: Expected number of rejected connection attempts.
        :param metric_aggregate: Per-server metric publication level.
        """

        suffix = f"max-{maximum}-aggregate-{metric_aggregate}"
        origin = self._services.httpbin(f"httpbin-{suffix}")
        ats = self.configure_connect_ats(suffix, maximum, metric_aggregate, origin)
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

        host = "www.this.origin.com"
        group = f"{host}.127.0.0.1:{origin.port}"
        metrics = self.wait_for_metrics(
            ats,
            (
                f"per_server.total_connection.{host} 5",
                f"per_server.blocked_connection.{host} {blocked}",
            ),
        )
        if metric_aggregate == 1:
            assert f"per_server.total_connection.{group} 5" in metrics
        else:
            for counter in ("current_connection", "total_connection", "blocked_connection"):
                assert f"per_server.{counter}.{group} " not in metrics

        hidden = self.wait_for_metrics(
            ats,
            (f"per_server.total_connection.{group} 5",),
            include_hidden=True,
        )
        assert "INVALID_INCOMING_DATA" not in hidden

    def configure_multi_group_ats(self, origin_a: HttpBinServer, origin_b: HttpBinServer) -> ATS:
        """Configure two origin groups under one hostname aggregate.

        :param origin_a: First HTTP origin and connection group.
        :param origin_b: Second HTTP origin and connection group.
        """

        ats = self._ats_factory.create("multi-group-ts")
        self.configure_common_records(ats)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|hostdb|conn_track",
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.metric_aggregate": 2,
                "proxy.config.http.per_server.connection.match": "both",
            })
        ats.remap_config.add_lines(
            (
                f"map http://multi.origin.com/a/ http://multi.origin.com:{origin_a.port}/",
                f"map http://multi.origin.com/b/ http://multi.origin.com:{origin_b.port}/",
            ))
        return ats

    def multi_group_request(self, ats: ATS, path: str, hold_seconds: int) -> CommandResult:
        """Hold one connection open through a selected aggregate group.

        :param ats: Traffic Server proxy that receives the request.
        :param path: Remap path selecting origin group ``a`` or ``b``.
        :param hold_seconds: Number of seconds the HTTPBin response is delayed.
        """

        return self._curl.run_for(
            ats,
            (
                f"--verbose --fail --silent --proxy '127.0.0.1:{ats.http_port}' "
                f"'http://multi.origin.com/{path}/delay/{hold_seconds}'"),
            timeout=hold_seconds + 10,
        )

    def run_multi_group_aggregate_case(self) -> None:
        """Verify SUM and MAX aggregates span two live connection groups."""

        origin_a = self._services.httpbin("multi-group-origin-a")
        origin_b = self._services.httpbin("multi-group-origin-b")
        ats = self.configure_multi_group_ats(origin_a, origin_b)
        origin_a.start()
        origin_b.start()
        ats.start()

        hold_seconds = 6
        with ThreadPoolExecutor(max_workers=5) as executor:
            requests = [executor.submit(self.multi_group_request, ats, "a", hold_seconds) for _ in range(2)]
            requests.extend(executor.submit(self.multi_group_request, ats, "b", hold_seconds) for _ in range(3))
            self.wait_for_metrics(
                ats,
                (
                    "per_server.total_connection.multi.origin.com 5",
                    "per_server.current_connection.multi.origin.com 5",
                    "per_server.current_connection_max.multi.origin.com 3",
                ),
            )
            results = [future.result(timeout=hold_seconds + 5) for future in requests]

        for result in results:
            assert result.returncode == 0, result.output
        self.wait_for_metrics(
            ats,
            (
                "per_server.current_connection.multi.origin.com 0",
                "per_server.current_connection_max.multi.origin.com 0",
            ),
        )

    def configure_metric_override_ats(self, origin_on: HttpBinServer, origin_off: HttpBinServer) -> ATS:
        """Enable metrics globally and disable them for one remap rule.

        :param origin_on: Origin whose remap keeps metrics enabled.
        :param origin_off: Origin whose remap overrides metrics to disabled.
        """

        ats = self._ats_factory.create("metric-override-ts")
        self.configure_common_records(ats)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|conn_track",
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.match": "port",
            })
        ats.remap_config.add_lines(
            (
                f"map http://metric-on.com/ http://127.0.0.1:{origin_on.port}/",
                f"map http://metric-off.com/ http://127.0.0.1:{origin_off.port}/"
                " @plugin=conf_remap.so"
                " @pparam=proxy.config.http.per_server.connection.metric_enabled=0",
            ))
        return ats

    def run_metric_override_case(self) -> None:
        """Verify a remap override suppresses both public and hidden metrics."""

        origin_on = self._services.httpbin("metric-on-origin")
        origin_off = self._services.httpbin("metric-off-origin")
        ats = self.configure_metric_override_ats(origin_on, origin_off)
        origin_on.start()
        origin_off.start()
        ats.start()
        enabled = self._curl.get(ats, "/get", headers={"Host": "metric-on.com"}, options="--verbose --silent")
        disabled = self._curl.get(ats, "/get", headers={"Host": "metric-off.com"}, options="--verbose --silent")
        assert enabled.returncode == 0, enabled.output
        assert disabled.returncode == 0, disabled.output

        on_group = f"127.0.0.1:{origin_on.port}"
        off_group = f"127.0.0.1:{origin_off.port}"
        metrics = self.wait_for_metrics(ats, (f"per_server.total_connection.{on_group} 1",))
        assert f"per_server.total_connection.{off_group}" not in metrics
        hidden = self.read_metrics(ats, include_hidden=True)
        assert f"per_server.total_connection.{on_group} 1" in hidden
        assert f"per_server.total_connection.{off_group}" not in hidden

    def configure_aggregate_only_without_host_ats(self, origin: HttpBinServer) -> ATS:
        """Configure aggregate-only publication for a match type without aggregates.

        :param origin: HTTP origin used for the single per-port group.
        """

        ats = self._ats_factory.create("aggregate-only-without-host-ts")
        self.configure_common_records(ats)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|conn_track",
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.metric_aggregate": 2,
                "proxy.config.http.per_server.connection.match": "port",
            })
        ats.remap_config.add_line(f"map http://agg-only.com/ http://127.0.0.1:{origin.port}/")
        return ats

    def run_aggregate_only_without_host_case(self) -> None:
        """Verify aggregate-only mode publishes a group when no host aggregate exists."""

        origin = self._services.httpbin("aggregate-only-without-host-origin")
        ats = self.configure_aggregate_only_without_host_ats(origin)
        origin.start()
        ats.start()
        result = self._curl.get(ats, "/get", headers={"Host": "agg-only.com"}, options="--verbose --silent")
        assert result.returncode == 0, result.output

        group = f"127.0.0.1:{origin.port}"
        metrics = self.wait_for_metrics(ats, (f"per_server.total_connection.{group} 1",))
        assert "per_server.total_connection.agg-only.com" not in metrics

    def run(self) -> None:
        """Run connection limits, aggregates, overrides, and fallback coverage."""

        self._dns.start()
        self.run_replay_case()
        self.run_connect_case(maximum=3, blocked=2, metric_aggregate=2)
        self.run_connect_case(maximum=0, blocked=0, metric_aggregate=1)
        self.run_multi_group_aggregate_case()
        self.run_metric_override_case()
        self.run_aggregate_only_without_host_case()


def test_per_server_connection_max(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS enforces and reports per-origin connection limits.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for DNS, origin, and verifier services.
    :param curl: Transport-aware curl command runner.
    """

    PerServerConnectionMaxScenario(ats_factory, services, curl).run()
