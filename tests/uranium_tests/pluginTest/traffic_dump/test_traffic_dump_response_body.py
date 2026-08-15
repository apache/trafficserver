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
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"
REPLAY_FILE = TEST_DIRECTORY / "replay" / "response_body.yaml"


class TrafficDumpResponseBodyScenario:
    """Validate response bodies written to traffic_dump replay files."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._dump_directory = self._ats.log_directory / "127"

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the origin for all response-body cases."""

        return services.verifier_server("server", REPLAY_FILE)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable full traffic_dump body capture on HTTP/1 and HTTP/2."""

        ats = ats_factory.create("ts", enable_tls=True)
        if not ats.plugin_exists("traffic_dump.so"):
            pytest.skip("traffic_dump.so is not installed")
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "server.pem",
            TEST_DIRECTORY / "ssl" / "server.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "traffic_dump",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.host_sni_policy": 2,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        ats.plugin_config.add_line(f"traffic_dump.so --logdir {ats.log_directory} --sample 1 --limit 1000000000 -b")
        return ats

    def run_traffic(self) -> None:
        """Replay the four HTTP/1 and HTTP/2 response cases."""

        client = self._services.verifier_client(
            "client",
            REPLAY_FILE,
            http_ports=[self._ats.http_port],
            https_ports=[self._ats.https_port],
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        result = client.run()
        assert result.returncode == 0, result.output

    def wait_for_dump(self, index: int) -> Path:
        """Wait for the replay file with @a index to be closed and visible."""

        path = self._dump_directory / f"{index:016d}"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if path.is_file() and path.stat().st_size:
                return path
            time.sleep(0.1)
        raise AssertionError(f"traffic_dump did not write {path}")

    def verify_dump(self, index: int, response_body: str | None = None) -> None:
        """Validate one dumped replay and its optional body text."""

        arguments: list[str | Path] = [
            sys.executable,
            TEST_DIRECTORY / "verify_replay.py",
            TEST_TOOLS / "lib" / "replay_schema.json",
            self.wait_for_dump(index),
        ]
        if response_body is not None:
            arguments.extend(("--response_body", response_body))
        result = self._ats.run(*arguments)
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Capture traffic and validate all serialized response bodies."""

        self._server.start()
        self._ats.start()
        self.run_traffic()
        self.verify_dump(0)
        self.verify_dump(1, "0000000 0000001 ")
        self.verify_dump(2, '12"34')
        self.verify_dump(3, "0000000 0000001 0000002 ")
        assert "Dumping body bytes: true" in self._ats.traffic_out.read_text(errors="replace")


def test_traffic_dump_response_body(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump serializes response bodies for HTTP/1 and HTTP/2."""

    TrafficDumpResponseBodyScenario(ats_factory, services).run()
