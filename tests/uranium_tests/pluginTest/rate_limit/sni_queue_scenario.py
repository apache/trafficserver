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
import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class RateLimitSniScenario:
    """Drive one rate_limit SNI queue or rejection disposition."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        *,
        queue_lines: tuple[str, ...],
        client_script: str,
        client_marker: str,
        traffic_marker: str,
        failure_expression: str,
    ) -> None:
        self._queue_lines = queue_lines
        self._client_script = client_script
        self._client_marker = client_marker
        self._traffic_marker = traffic_marker
        self._failure_expression = failure_expression
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable the SNI limiter with a one-connection active limit."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, server_args=["-f", "-F"])
        if not ats.plugin_exists("rate_limit.so"):
            pytest.skip("rate_limit.so is required")
        config = ["selector:", "  - sni: rate.limited.com", "    limit: 1", *self._queue_lines]
        ats.write_config_file("rate_limit.config", "\n".join(config) + "\n")
        ats.plugin_config.add_line(f"rate_limit.so {ats.config_directory}/rate_limit.config")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rate_limit",
        })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Run the shell driver that creates concurrent TLS handshakes."""

        return services.process(
            "client",
            (
                "/bin/bash",
                TEST_DIRECTORY / self._client_script,
                "127.0.0.1",
                str(self._ats.https_port),
                "rate.limited.com",
            ),
        )

    def verify(self, result: CommandResult) -> None:
        """Require the target path and reject memory-safety or accounting faults."""

        assert result.returncode == 0, result.output
        assert self._client_marker in result.stdout
        traffic_out = wait_for_file_lines(self._ats.traffic_out, self._traffic_marker, 1)
        assert re.search(self._failure_expression, traffic_out) is None, traffic_out

    def run(self) -> None:
        """Start ATS, drive handshake churn, and inspect traffic.out."""

        self._ats.start()
        self.verify(self._client.run(timeout=30))
