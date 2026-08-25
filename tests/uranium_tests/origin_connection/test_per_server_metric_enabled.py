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

import re
import time

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer


class PerServerMetricScenario:
    """Track and reap one pooled origin connection with metrics enabled."""

    _replay = "per_server_metric_enabled.replay.yaml"
    _keep_alive_timeout = 2
    _stat_sync_interval_ms = 500

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the persistent-connection verifier origin."""

        return services.verifier_server("server", self._replay, https_ports=[])

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable per-server connection metrics with a zero idle minimum."""

        ats = ats_factory.create("ts")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        ats.records.update(
            {
                "proxy.config.raw_stat_sync_interval_ms": self._stat_sync_interval_ms,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http_ss|conn_track",
                "proxy.config.http.per_server.connection.metric_enabled": 1,
                "proxy.config.http.per_server.connection.metric_prefix": "bar",
                "proxy.config.http.per_server.connection.match": "port",
                "proxy.config.http.per_server.connection.min": 0,
                "proxy.config.http.keep_alive_no_activity_timeout_out": self._keep_alive_timeout,
                "proxy.config.http.server_session_sharing.pool": "global",
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the transaction that leaves one origin session pooled."""

        return services.verifier_client("client", self._replay, http_ports=[self._ats.http_port])

    def metric(self, *arguments: str) -> str:
        """Run a metric command and return its successful output."""

        result = self._ats.traffic_ctl("metric", *arguments)
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Verify the gauges, totals, tracker report, and underflow diagnostic."""

        self._server.start()
        self._ats.start()
        client = self._client.run()
        assert client.returncode == 0, client.output
        time.sleep(self._keep_alive_timeout * 3)

        current = self.metric("get", "proxy.process.http.current_server_connections")
        assert "proxy.process.http.current_server_connections 0" in current
        per_server = self.metric("match", "per_server")
        key = f"bar.127.0.0.1:{self._server.http_port}"
        assert f"per_server.current_connection.{key} 0" in per_server
        assert f"per_server.total_connection.{key} 1" in per_server

        tracker = self._ats.traffic_ctl(
            "rpc",
            "invoke",
            "get_connection_tracker_info",
            "-p",
            "table: outbound",
            "-f",
            "json",
        )
        assert tracker.returncode == 0, tracker.output
        assert re.search(r'"(count|current)":\s*"?0"?', tracker.output)
        diags = self._ats.diags_log.read_text(errors="replace")
        assert "Number of tracked connections should be greater than or equal to zero" not in diags


def test_per_server_metric_enabled(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Per-server metrics do not prevent idle origin connection reaping."""

    PerServerMetricScenario(ats_factory, services).run()
