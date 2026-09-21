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
import sys

from tools.uranium.services import ATS, ATSFactory, CommandResult, ServiceFactory, VerifierServer


class Http2MaxActiveStreamsScenario:
    """Drive concurrent inbound streams past the configured active-stream cap."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._directory = Path(__file__).parent

    def configure_server(self, name: str, replay: Path) -> VerifierServer:
        """Create the verifier origin for one policy case."""

        return self._services.verifier_server(f"server-{name}", replay)

    def configure_ats(self, name: str, policy: int, server: VerifierServer) -> ATS:
        """Configure the stream cap and enforcement policy."""

        ats = self._ats_factory.create(f"ts-{name}", enable_tls=True, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2",
                "proxy.config.http2.max_active_streams_in": 2,
                "proxy.config.http2.max_active_streams_policy_in": policy,
                "proxy.config.http2.max_concurrent_streams_in": 100,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}")
        return ats

    def run_client(self, name: str, ats: ATS) -> CommandResult:
        """Run the bespoke HTTP/2 client with four simultaneous streams."""

        return self._services.process(
            f"client-{name}",
            [
                sys.executable,
                self._directory / "clients/h2_max_active_streams.py",
                str(ats.https_port),
                "--streams",
                "4",
                "--probe-from",
                "5",
            ],
        ).run()

    def run_case(self, name: str, replay_name: str, policy: int) -> None:
        """Execute and validate one active-stream policy."""

        server = self.configure_server(name, self._directory / "replay" / replay_name)
        ats = self.configure_ats(name, policy, server)
        server.start()
        ats.start()
        result = self.run_client(name, ats)
        assert "GOAWAY" not in result.stdout
        if policy == 1:
            assert "stream 5: RST_STREAM error_code=7" in result.stdout
            assert "stream 7: RST_STREAM error_code=7" in result.stdout
            assert "active streams cap reached" in ats.traffic_out.read_text(errors="replace")
        else:
            assert "RST_STREAM error_code=7" not in result.stdout

    def run(self) -> None:
        """Exercise enforce and advisory policies."""

        self.run_case("enforce", "http2_max_active_streams_enforce.replay.yaml", 1)
        self.run_case("advisory", "http2_max_active_streams_advisory.replay.yaml", 0)


def test_http2_max_active_streams(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The active-stream cap refuses streams without desynchronizing HPACK."""

    Http2MaxActiveStreamsScenario(ats_factory, services).run()
