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
import sys

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
REPLAY_FILE = TEST_DIRECTORY / "replays" / "h2_malformed_request_logging.replay.yaml"
MALFORMED_CLIENT = TEST_DIRECTORY / "malformed_h2_request_client.py"


class MalformedH2RequestLoggingScenario:
    """Verify malformed HTTP/2 requests are logged before transaction creation."""

    CASES = (
        ("connect-missing-authority", "malformed-connect", "CONNECT", "/"),
        ("get-missing-path", "malformed-get-missing-path", "GET", "https://missing-path.example/"),
        (
            "get-connection-header",
            "malformed-get-connection",
            "GET",
            "https://bad-connection.example/bad-connection",
        ),
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._valid_client = self.configure_valid_client(services)

    @staticmethod
    def configure_server(services: ServiceFactory) -> VerifierServer:
        """Create the origin for the healthy control requests."""

        return services.verifier_server("malformed-request-server", REPLAY_FILE)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure an HTTP/2-only listener and the malformed-request log format."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.storage_config.add_line("")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|hpack|http2",
                "proxy.config.http.server_ports": f"{ats.https_port}:ssl",
                "proxy.config.http.connect_ports": self._server.http_port,
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}/")
        ats.allow_private_connect(("CONNECT", "GET"))
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "malformed_h2_request",
                                    "format": ("uuid=%<{uuid}cqh> cqpv=%<cqpv> cqhm=%<cqhm> "
                                               "crc=%<crc> sstc=%<sstc> pqu=%<pqu>"),
                                }
                            ],
                        "logs": [{
                            "filename": "squid",
                            "format": "malformed_h2_request",
                            "mode": "ascii"
                        }],
                    }
            })
        return ats

    def configure_valid_client(self, services: ServiceFactory) -> ProcessService:
        """Create the healthy GET and CONNECT control client."""

        return services.verifier_client("valid-request-client", REPLAY_FILE, https_ports=[self._ats.https_port])

    def malformed_request(self, scenario: str) -> str:
        """Run the raw-frame client for one malformed shape."""

        process = self._services.process(
            f"malformed-client-{scenario}",
            [sys.executable, MALFORMED_CLIENT, str(self._ats.https_port), scenario],
        )
        result = process.run(timeout=10)
        assert re.search(r"Received (RST_STREAM on stream 1 with error code 1|GOAWAY with error code [01])", result.stdout)
        return result.output

    def validate_logs(self) -> None:
        """Check malformed and healthy transactions in the access log."""

        wait_for_file_lines(
            self._ats.log_directory / "squid.log",
            "crc=ERR_INVALID_REQ",
            len(self.CASES),
        )
        squid_log = wait_for_file_lines(
            self._ats.log_directory / "squid.log",
            r"uuid=valid-get",
            1,
        )
        for _scenario, uuid, method, url in self.CASES:
            expected = (rf"uuid={uuid} cqpv=http/2 cqhm={method} "
                        rf"crc=ERR_INVALID_REQ sstc=0 pqu={re.escape(url)}")
            assert re.search(expected, squid_log)
        assert re.search(r"uuid=valid-get cqpv=http/2 cqhm=GET ", squid_log)
        if "uuid=valid-connect" in squid_log:
            assert re.search(r"uuid=valid-connect .*crc=ERR_INVALID_REQ", squid_log) is None
        assert re.search(r"uuid=valid-get .*crc=ERR_INVALID_REQ", squid_log) is None

    def run(self) -> None:
        """Run malformed wire requests followed by healthy control traffic."""

        self._server.start()
        self._ats.start()
        for scenario, _uuid, _method, _url in self.CASES:
            self.malformed_request(scenario)
        self._valid_client.run()
        server_output = self._server.output
        for _scenario, uuid, _method, _url in self.CASES:
            assert f"uuid: {uuid}" not in server_output
        assert re.search(r"GET /get HTTP/1\.1\nuuid: valid-connect", server_output)
        assert re.search(r"GET /valid-get HTTP/1\.1\n(?:.*\n)*uuid: valid-get", server_output)
        self.validate_logs()
        assert "recv headers malformed request" in self._ats.traffic_out.read_text(errors="replace")


def test_h2_malformed_request_logging(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Malformed HTTP/2 requests receive ERR_INVALID_REQ access-log records."""

    MalformedH2RequestLoggingScenario(ats_factory, services).run()
