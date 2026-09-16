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
"""Verify HTTP/2 session errors are accounted once per connection."""

from pathlib import Path
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines, wait_for_metric

TEST_DIRECTORY = Path(__file__).parent


class SessionErrorScenario:
    """Send one category of HTTP/2 session error and inspect accounting."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        mode: str,
        trusted: bool,
    ) -> None:
        """Configure the requested HTTP/2 error path.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param services: Factory for the helper client and optional origin.
        :param curl: Curl runner used to probe a blocked connection.
        :param mode: Error-generation mode accepted by the helper.
        :param trusted: Whether localhost bypasses all enforcement.
        """

        self._mode = mode
        self._trusted = trusted
        self._curl = curl
        self._origin = self.configure_origin(services) if mode == "limit" else None
        self._ats = self.configure_ats(ats_factory)
        self._client = services.process(
            "h2-error-client",
            (sys.executable, TEST_DIRECTORY / "h2_session_errors.py", "--port", str(self._ats.https_port), "--mode", mode),
        )

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the origin required by admitted concurrent streams.

        :param services: Factory for the microserver origin.
        """

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "POST / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n",
                "body": "OK"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the session-error rule and optional trusted bypass.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        if not ats.plugin_exists("abuse_shield.so"):
            pytest.skip("abuse_shield.so is required")
        ats.add_default_ssl_files()
        ats.records.update({"proxy.config.http2.max_concurrent_streams_in": 2})
        if self._origin is not None:
            ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        trusted_line = ""
        if self._trusted:
            ats.write_config_file("trusted.yaml", "trusted_ips:\n  - 127.0.0.1\n")
            trusted_line = f"  trusted_ips_file: {ats.config_directory / 'trusted.yaml'}\n"
        actions = "log, block, close" if self._mode == "block" else "log"
        ats.write_config_file(
            "abuse_shield.yaml",
            "global:\n  ip_tracking:\n    slots: 1000\n  log_interval_sec: 0\n"
            f"{trusted_line}rules:\n  - name: session_errors\n    filter:\n      max_h2_error_rate: 1\n"
            f"    action: [{actions}]\nenabled: true\n",
        )
        ats.plugin_config.add_line("abuse_shield.so abuse_shield.yaml")
        return ats

    def run(self) -> None:
        """Run the helper and verify event, stream, logging, and block metrics."""

        if self._origin is not None:
            self._origin.start()
        self._ats.start()
        result = self._client.run(timeout=30)
        assert result.returncode == 0, result.output
        wait_for_file_lines(self._ats.diags_log, "Plugin initialized", 1)
        if self._mode == "limit":
            wait_for_file_lines(self._ats.diags_log, "beyond max_concurrent limit", 1)

        expected = 0 if self._trusted or self._mode == "normal" else 8
        streams = 16 if self._mode == "limit" else 0
        if expected:
            wait_for_metric(self._ats, "abuse_shield.h2.events", expected)
        if streams:
            wait_for_metric(self._ats, "proxy.process.http2.total_client_streams", streams)
        metrics = self._ats.traffic_ctl(
            "metric",
            "get",
            "abuse_shield.h2.events",
            "proxy.process.http2.total_client_streams",
            "abuse_shield.actions.logged",
        )
        assert metrics.returncode == 0, metrics.output
        assert re.search(rf"abuse_shield.h2.events\s+{expected}\b", metrics.stdout)
        assert re.search(rf"proxy.process.http2.total_client_streams\s+{streams}\b", metrics.stdout)
        logged = r"0" if expected == 0 else r"[1-9][0-9]*"
        assert re.search(rf"abuse_shield.actions.logged\s+{logged}\b", metrics.stdout)

        if self._mode == "block":
            rejected = self._curl.run(f"--insecure --silent --max-time 2 https://127.0.0.1:{self._ats.https_port}/")
            assert rejected.returncode in (28, 35, 52, 55, 56), rejected.output
            block_metrics = self._ats.traffic_ctl(
                "metric",
                "get",
                "abuse_shield.actions.blocked",
                "abuse_shield.connections.rejected",
                "abuse_shield.actions.closed",
                "abuse_shield.actions.close_failed",
            )
            assert block_metrics.returncode == 0, block_metrics.output
            assert re.search(r"abuse_shield.actions.blocked\s+[1-9][0-9]*", block_metrics.stdout)
            assert re.search(r"abuse_shield.connections.rejected\s+1\b", block_metrics.stdout)
            assert re.search(r"abuse_shield.actions.closed\s+0\b", block_metrics.stdout)
            assert re.search(r"abuse_shield.actions.close_failed\s+0\b", block_metrics.stdout)


@pytest.mark.parametrize(
    ("mode", "trusted"),
    (
        pytest.param("invalid", False, id="invalid"),
        pytest.param("limit", False, id="concurrent-limit"),
        pytest.param("received", False, id="received"),
        pytest.param("normal", False, id="normal"),
        pytest.param("invalid", True, id="trusted-invalid"),
        pytest.param("block", False, id="block"),
    ),
)
def test_abuse_shield_h2_errors(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    mode: str,
    trusted: bool,
) -> None:
    """Session errors need no HTTP transaction and obey trusted policy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the helper client and optional origin.
    :param curl: Curl runner used to probe a blocked connection.
    :param mode: Error-generation mode accepted by the helper.
    :param trusted: Whether localhost bypasses all enforcement.
    """

    SessionErrorScenario(ats_factory, services, curl, mode, trusted).run()
