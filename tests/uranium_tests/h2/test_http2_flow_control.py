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
"""Cover flow-control cases that require configuration-only or bespoke clients."""

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class InvalidFlowControlPolicyScenario:
    """Start ATS with one malformed inbound or outbound policy value."""

    def __init__(self, ats_factory: ATSFactory, case: str) -> None:
        self._case = case
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the malformed policy on the direction selected by the case."""

        direction = "out" if self._case == "out-h2" else "in"
        ats = ats_factory.create(f"invalid-policy-{self._case}", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 3,
                "proxy.config.diags.debug.tags": "http",
                f"proxy.config.http2.flow_control.policy_{direction}": 23,
            })
        return ats

    def run(self) -> None:
        """Start ATS and verify it reports the invalid record."""

        self._ats.start()
        direction = "out" if self._case == "out-h2" else "in"
        wait_for_file_lines(self._ats.diags_log, rf"ERROR.*proxy.config.http2.flow_control.policy_{direction}", 1)


class DynamicWindowSettingsCapScenario:
    """Withhold SETTINGS ACKs until ATS reaches its outstanding-frame cap."""

    REPLAY = TEST_DIRECTORY / "replay" / "http2_settings_ack_stall.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> VerifierServer:
        """Create the origin for the custom HTTP/2 client's requests."""

        return services.verifier_server("settings-cap-origin", cls.REPLAY)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable dynamic windows with only two concurrent inbound streams."""

        ats = ats_factory.create("settings-cap-ats", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2",
                "proxy.config.http.insert_response_via_str": 2,
                "proxy.config.http2.active_timeout_in": 5,
                "proxy.config.http2.flow_control.policy_in": 2,
                "proxy.config.http2.max_concurrent_streams_in": 2,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        return ats

    def run(self) -> None:
        """Run the non-acking client and validate SETTINGS_TIMEOUT."""

        self._server.start()
        self._ats.start()
        result = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "clients" / "h2_settings_ack_stall.py",
            str(self._ats.https_port),
            timeout=20,
        )
        assert result.returncode == 0, result.output
        assert "GOAWAY error_code=4" in result.stdout
        wait_for_file_lines(
            self._ats.diags_log,
            r"ERROR: HTTP/2 connection error code=0x04.*send settings too many outstanding SETTINGS frames",
            1,
        )


@pytest.mark.parametrize("case", ("in-http1-content-length", "in-http1-chunked", "in-h2", "out-h2"))
def test_invalid_http2_flow_control_policy(ats_factory: ATSFactory, case: str) -> None:
    """Malformed policies are rejected for every former origin topology."""

    InvalidFlowControlPolicyScenario(ats_factory, case).run()


def test_dynamic_window_settings_cap(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Dynamic flow control caps outstanding unacknowledged SETTINGS frames."""

    DynamicWindowSettingsCapScenario(ats_factory, services).run()
