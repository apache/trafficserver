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
import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def add_empty_response(origin: OriginServer, host: str) -> None:
    """Configure the microserver response shared by continuation scenarios."""

    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )


class ContinuationMetricsScenario:
    """Exercise continuation accounting with many concurrent client sessions."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        protocol: str,
        plugin: str,
        request_count: int,
    ) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl
        self._protocol = protocol
        self._plugin = plugin
        self._request_count = request_count
        self._origin = self.configure_origin()
        self._ats = self.configure_ats()

    def configure_origin(self) -> OriginServer:
        """Create the repeated empty-response origin."""

        origin = self._services.origin("origin")
        add_empty_response(origin, "continuations.test")
        return origin

    def configure_ats(self) -> ATS:
        """Load the accounting plugin and configure HTTP or HTTP/2 ingress."""

        enable_tls = self._protocol == "h2"
        ats = self._ats_factory.create("ts", enable_tls=enable_tls, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": self._plugin.removesuffix(".so"),
                "proxy.config.cache.enable_read_while_writer": 0,
                **({
                    "proxy.config.http2.max_concurrent_streams_in": 65535
                } if enable_tls else {}),
            })
        ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{self._plugin}")
        ats.plugin_config.add_line(self._plugin)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def run_clients(self) -> None:
        """Issue independent curl processes concurrently."""

        if self._protocol == "h2" and not self._curl.supports("http2"):
            pytest.skip("curl HTTP/2 support is required")
        if self._protocol == "h2" and self._curl.uses_uds:
            pytest.skip("HTTP/2 continuation coverage requires a TCP listener")

        url = (
            f"https://127.0.0.1:{self._ats.https_port}/" if self._protocol == "h2" else f"http://127.0.0.1:{self._ats.http_port}/")
        options = ["--silent", "--show-error", "--header", "Connection: close"]
        if self._protocol == "h2":
            options.extend(["--insecure", "--http2"])

        def request(_index: int) -> None:
            result = self._curl.run_for(self._ats, *options, url)
            assert result.returncode in (0, 2), result.output

        with ThreadPoolExecutor(max_workers=min(32, self._request_count)) as executor:
            list(executor.map(request, range(self._request_count)))

    def wait_for_metric(self, name: str, expected: int = 1) -> None:
        """Wait for a plugin metric to reach @a expected."""

        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            result = self._ats.traffic_ctl("metric", "get", name)
            if result.returncode == 0 and result.stdout.rstrip().endswith(f" {expected}"):
                return
            time.sleep(0.1)
        raise AssertionError(f"Metric {name} did not reach {expected}:\n{result.output}")

    def metrics(self, prefix: str) -> dict[str, int]:
        """Return all integer metrics under @a prefix."""

        result = self._ats.traffic_ctl("metric", "match", prefix)
        assert result.returncode == 0, result.output
        return {name: int(value) for name, value in re.findall(r"^(\S+)\s+(-?\d+)$", result.stdout, re.MULTILINE)}

    def run_traffic(self) -> None:
        """Start the topology, issue traffic, and flush plugin metrics."""

        self._origin.start()
        self._ats.start()
        self.run_clients()
        result = self._ats.traffic_ctl("plugin", "msg", "done", "done")
        assert result.returncode == 0, result.output


class DoubleContinuationScenario(ContinuationMetricsScenario):
    """Verify two global continuations observe identical hook counts."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, protocol: str) -> None:
        super().__init__(
            ats_factory,
            services,
            curl,
            protocol=protocol,
            plugin="continuations_verify.so",
            request_count=25 if protocol == "h2" else 55,
        )

    def run(self) -> None:
        """Compare session and transaction close metrics."""

        self.run_traffic()
        self.wait_for_metric("continuations_verify.test.done")
        metrics = self.metrics("continuations_verify")
        for scope in ("ssn", "txn"):
            assert metrics[f"continuations_verify.{scope}.close.1"] > 0
            assert metrics[f"continuations_verify.{scope}.close.1"] == metrics[f"continuations_verify.{scope}.close.2"]
        assert metrics["continuations_verify.txn.close.1"] == self._request_count


class OpenCloseScenario(ContinuationMetricsScenario):
    """Verify session and transaction hooks open and close in order."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, protocol: str) -> None:
        super().__init__(
            ats_factory,
            services,
            curl,
            protocol=protocol,
            plugin="ssntxnorder_verify.so",
            request_count=100,
        )

    def run(self) -> None:
        """Validate ordering and balanced start/close metrics."""

        self.run_traffic()
        self.wait_for_metric("ssntxnorder_verify.test.done")
        metrics = self.metrics("ssntxnorder_verify")
        assert metrics["ssntxnorder_verify.err"] == 0
        for scope in ("ssn", "txn"):
            assert metrics[f"ssntxnorder_verify.{scope}.start"] > 0
            assert metrics[f"ssntxnorder_verify.{scope}.start"] == metrics[f"ssntxnorder_verify.{scope}.close"]
        assert metrics["ssntxnorder_verify.txn.start"] == self._request_count


class SessionIdScenario:
    """Verify session identifiers are unique across HTTP/1 and HTTP/2."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._services = services
        self._curl = curl
        self._origin = services.origin("origin")
        add_empty_response(self._origin, "example.com")
        self._ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)

    def configure_ats(self) -> None:
        """Load the session-id verifier and configure the origin mapping."""

        self._ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "session_id_verify",
                "proxy.config.cache.enable_read_while_writer": 0,
            })
        self._ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/continuations/plugins/.libs/session_id_verify.so")
        self._ats.plugin_config.add_line("session_id_verify.so")
        self._ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")

    def run_requests(self, options: list[str], url: str, count: int) -> None:
        """Run one protocol's independent client sessions in parallel."""

        def request(_index: int) -> None:
            result = self._curl.run_for(self._ats, *options, url)
            assert result.returncode in (0, 2), result.output

        with ThreadPoolExecutor(max_workers=32) as executor:
            list(executor.map(request, range(count)))

    def run(self) -> None:
        """Exercise both ingress protocols and count unique-id diagnostics."""

        if not self._curl.supports("http2"):
            pytest.skip("curl HTTP/2 support is required")
        self.configure_ats()
        self._origin.start()
        self._ats.start()
        count = 100
        self.run_requests(
            ["--silent", "--show-error", "--header", "Connection: close"],
            f"http://127.0.0.1:{self._ats.http_port}/",
            count,
        )
        expected = count
        if not self._curl.uses_uds:
            self.run_requests(
                ["--silent", "--show-error", "--insecure", "--http2"],
                f"https://127.0.0.1:{self._ats.https_port}/",
                count,
            )
            expected += count
        session_ids = re.findall(r"session id: ([^\n]+)", self._ats.traffic_out.read_text(errors="replace"))
        assert len(session_ids) == expected
        assert len(set(session_ids)) == expected


@pytest.mark.parametrize("protocol", ["http1", "h2"])
def test_double_continuation_counts(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    protocol: str,
) -> None:
    """Two continuations observe the same session and transaction hooks."""

    DoubleContinuationScenario(ats_factory, services, curl, protocol=protocol).run()


@pytest.mark.parametrize("protocol", ["http1", "h2"])
def test_continuation_open_close_order(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    protocol: str,
) -> None:
    """Session and transaction hooks open and close in the correct order."""

    OpenCloseScenario(ats_factory, services, curl, protocol=protocol).run()


def test_session_ids_are_unique(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Session identifiers remain unique across HTTP/1 and HTTP/2."""

    SessionIdScenario(ats_factory, services, curl).run()
