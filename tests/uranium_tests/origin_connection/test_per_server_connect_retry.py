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

import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory

STAT_SYNC_INTERVAL_MS = 500


def test_per_server_connect_retry(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A failed origin connect returns its reservation before ATS retries.

    The first connection to an origin of unknown protocol goes through a
    coalescing ConnectingEntry, which holds the transaction's per-server
    reservation. Retries run inline on the failure event, so with a limit of
    one connection they are throttled if that reservation is still held.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory used to allocate an unused origin port.
    :param curl: Transport-aware curl command runner.
    """

    if curl.uses_uds:
        pytest.skip("Connection limit coverage requires TCP client connections")

    closed_port = services.allocate_port()
    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_connect|conn_track",
            "proxy.config.raw_stat_sync_interval_ms": STAT_SYNC_INTERVAL_MS,
            "proxy.config.http.connect_attempts_max_retries": 2,
            "proxy.config.http.connect_attempts_retry_backoff_base": 0,
            "proxy.config.http.per_server.connection.max": 1,
            "proxy.config.http.per_server.connection.metric_enabled": 1,
            "proxy.config.http.per_server.connection.match": "port",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{closed_port}")
    ats.start()

    result = curl.get(ats, "/get", options="--verbose --silent")
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 502" in result.stderr, result.output

    group = f"127.0.0.1:{closed_port}"
    required = (
        f"per_server.current_connection.{group} 0",
        f"per_server.blocked_connection.{group} 0",
    )
    deadline = time.monotonic() + 10
    metrics = ""
    while time.monotonic() < deadline:
        metric_result = ats.traffic_ctl("metric", "match", "per_server")
        assert metric_result.returncode == 0, metric_result.output
        metrics = metric_result.output
        if all(fragment in metrics for fragment in required):
            break
        time.sleep(0.1)
    else:
        pytest.fail(f"Timed out waiting for per-server metrics {required!r}:\n{metrics}")

    throttled = ats.traffic_ctl("metric", "get", "proxy.process.http.origin_connections_throttled_out")
    assert throttled.returncode == 0, throttled.output
    assert "proxy.process.http.origin_connections_throttled_out 0" in throttled.stdout, throttled.output
    assert "too many connections" not in ats.diags_log.read_text(errors="replace")
