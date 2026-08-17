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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"
REPLAY_FILE = TEST_DIRECTORY / "replay" / "http3.yaml"


class TrafficDumpHttp3Scenario:
    """Capture and validate an HTTP/3 session with traffic_dump."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        """Configure the HTTP/3 capture scenario.

        :param ats_factory: Factory that owns the ATS instance.
        :param services: Factory that owns the verifier processes.
        """

        if not ats_factory.has_feature("TS_USE_QUIC"):
            pytest.skip("ATS was built without QUIC")
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("traffic_dump.so"):
            pytest.skip("traffic_dump.so is not installed")
        self._client = self.configure_client(services)
        self._dump = self._ats.log_directory / "127" / "0000000000000000"

    @staticmethod
    def configure_server(services: ServiceFactory) -> VerifierServer:
        """Create the HTTP and TLS verifier origin.

        :param services: Factory that owns the verifier origin.
        """

        return services.verifier_server(
            "server-http3",
            REPLAY_FILE,
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable QUIC ingress and traffic_dump session capture.

        :param ats_factory: Factory that owns the ATS instance.
        """

        ats = ats_factory.create("ts", enable_tls=True, enable_quic=True)
        ats.set_startup_timeout(60)
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
                "proxy.config.diags.debug.tags": "traffic_dump|quic",
                "proxy.config.http.insert_age_in_response": 0,
                "proxy.config.quic.qlog.file_base": str(ats.log_directory / "qlog_dir"),
                "proxy.config.quic.server.stateless_retry_enabled": 0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.host_sni_policy": 2,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_lines(
            (
                f"map https://www.client_only_tls.com/ http://127.0.0.1:{self._server.http_port}",
                f"map https://www.tls.com/ https://127.0.0.1:{self._server.https_port}",
                f"map / http://127.0.0.1:{self._server.http_port}",
            ))
        ats.plugin_config.add_line(
            f'traffic_dump.so --logdir {ats.log_directory} --sample 1 --limit 1000000000 '
            '--sensitive-fields "cookie,set-cookie,x-request-1,x-request-2"')
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [{
                                "name": "basic",
                                "format": "%<cluc>: Read result: %<crc>:%<crsc>:%<chm>, Write result: %<cwr>",
                            }],
                        "logs": [{
                            "filename": "transactions",
                            "format": "basic"
                        }],
                    }
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Create the verifier client that opens the HTTP/3 session.

        :param services: Factory that owns the verifier client.
        """

        return services.verifier_client(
            "client-http3",
            REPLAY_FILE,
            http_ports=[self._ats.http_port],
            https_ports=[self._ats.https_port],
            http3_ports=[self._ats.https_port],
            ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
            ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
        )

    def wait_for_dump(self) -> None:
        """Wait for traffic_dump to close the first session replay."""

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self._dump.is_file() and self._dump.stat().st_size:
                return
            time.sleep(0.1)
        raise AssertionError(f"traffic_dump did not write {self._dump}")

    def verify_dump(self) -> None:
        """Validate the generated replay schema and HTTP/3 metadata."""

        result = self._ats.run(
            sys.executable,
            TEST_DIRECTORY / "verify_replay.py",
            TEST_TOOLS / "lib" / "replay_schema.json",
            self._dump,
            "--client-http-version",
            "3",
            "--client-protocols",
            "tcp,ip",
        )
        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Run the HTTP/3 traffic and validate its completed session dump."""

        self._server.start()
        self._ats.start()
        result = self._client.run()
        assert result.returncode == 0, result.output
        self.wait_for_dump()
        self.verify_dump()
        traffic = wait_for_file_lines(self._ats.traffic_out, r"Finish a session with log file of.*bytes", 1)
        assert f"Initialized with log directory: {self._ats.log_directory}" in traffic


@pytest.mark.manual(reason="HTTP/3 session events are not yet fully supported")
def test_traffic_dump_http3(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump serializes a completed HTTP/3 session.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the verifier processes.
    """

    TrafficDumpHttp3Scenario(ats_factory, services).run()
