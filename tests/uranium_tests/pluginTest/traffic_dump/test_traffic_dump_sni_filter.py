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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"


class TrafficDumpSniFilterScenario:
    """Verify traffic_dump records only the configured TLS SNI."""

    _replay = "replay/various_sni.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("traffic_dump.so"):
            pytest.skip("traffic_dump.so is required")
        self._client = self.configure_client(services)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the TLS origin for the SNI sessions."""

        return services.verifier_server(
            "server-various-sni",
            self._replay,
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the bob.com filter and permissive SNI policy."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "server.pem",
            TEST_DIRECTORY / "ssl" / "server.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "traffic_dump",
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.host_sni_policy": 2,
                "proxy.config.ssl.TLSv1_3.enabled": 0,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._server.https_port}")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n- fqdn: bob.com\n  verify_client: NONE\n  host_sni_policy: PERMISSIVE\n",
        )
        ats.plugin_config.add_line(f'traffic_dump.so --logdir {ats.log_directory} --sample 1 --sni-filter "bob.com"')
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the client that opens matching, mismatched, and absent-SNI sessions."""

        return services.verifier_client(
            "client-various-sni",
            self._replay,
            https_ports=[self._ats.https_port],
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )

    def run(self) -> None:
        """Replay the sessions and inspect the sole dumped connection."""

        self._server.start()
        self._ats.start()
        result = self._client.run()
        assert result.returncode == 0, result.output
        dump_directory = self._ats.log_directory / "127"
        first = dump_directory / "0000000000000000"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not first.is_file():
            time.sleep(0.1)
        assert first.is_file()
        assert not (dump_directory / "0000000000000001").exists()
        assert not (dump_directory / "0000000000000002").exists()

        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "Filtering to only dump connections with SNI: bob.com" in traffic_out
        assert "Ignore HTTPS session with non-filtered SNI: dave" in traffic_out
        assert "Initialized with sample pool size of 1 bytes and unlimited disk utilization" in traffic_out
        verify = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "verify_replay.py",
            TEST_TOOLS / "lib" / "replay_schema.json",
            first,
            "--client-protocols",
            "http,tls,tcp,ip",
            "--client-tls-features",
            "sni:bob.com,proxy-verify-mode:0,proxy-provided-cert:true",
        )
        assert verify.returncode == 0, verify.output


def test_traffic_dump_sni_filter(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump's SNI filter records only matching TLS sessions."""

    TrafficDumpSniFilterScenario(ats_factory, services).run()
