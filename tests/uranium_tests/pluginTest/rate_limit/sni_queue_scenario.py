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


def run_rate_limit_sni(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    *,
    queue_lines: tuple[str, ...],
    client_script: str,
    client_marker: str,
    traffic_marker: str,
    failure_expression: str,
) -> None:
    """Drive one rate_limit SNI queue or rejection disposition.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param queue_lines: YAML lines configuring the SNI handshake queue.
    :param client_script: Client script relative to this test directory.
    :param client_marker: Required marker in the client output.
    :param traffic_marker: Regular expression identifying the required ATS diagnostic.
    :param failure_expression: Regular expression forbidden in completed ATS output.
    """

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable the SNI limiter with a one-connection active limit.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, server_args=["-f", "-F"])
        if not ats.plugin_exists("rate_limit.so"):
            pytest.skip("rate_limit.so is required")
        config = ["selector:", "  - sni: rate.limited.com", "    limit: 1", *queue_lines]
        ats.write_config_file("rate_limit.config", "\n".join(config) + "\n")
        ats.plugin_config.add_line(f"rate_limit.so {ats.config_directory}/rate_limit.config")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rate_limit",
        })
        return ats

    def configure_client(services: ServiceFactory) -> ProcessService:
        """Run the shell driver that creates concurrent TLS handshakes.

        :param services: Factory owning support services and their cleanup.
        """

        return services.process(
            "client",
            (
                "/bin/bash",
                TEST_DIRECTORY / client_script,
                "127.0.0.1",
                str(_ats.https_port),
                "rate.limited.com",
                str(_ats.traffic_out),
            ),
        )

    def verify(result: CommandResult) -> None:
        """Require the target path and reject memory-safety or accounting faults.

        :param result: Completed command result to validate.
        """

        assert result.returncode == 0, result.output
        assert client_marker in result.stdout
        wait_for_file_lines(_ats.traffic_out, traffic_marker, 1)
        _ats.stop()
        traffic_out = _ats.traffic_out.read_text(errors="replace")
        if client_script == "rate_limit_sni_expiry_client.sh":
            assert "Queueing the VC" in traffic_out, traffic_out
            assert "Queued VC is too old" in traffic_out, traffic_out
        assert re.search(failure_expression, traffic_out) is None, traffic_out

    _ats = configure_ats(ats_factory)
    _client = configure_client(services)

    _ats.start()
    verify(_client.run(timeout=30))
