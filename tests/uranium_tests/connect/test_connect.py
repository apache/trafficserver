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

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    HttpBinServer,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class CurlConnectScenario:
    """Exercise curl's HTTP/1.1 proxy-tunnel mode and its access log entry."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> HttpBinServer:
        """Create the HTTP origin reached after CONNECT succeeds."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow CONNECT only to the allocated origin port."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.server_ports": str(ats.http_port),
                "proxy.config.http.connect_ports": str(self._origin.port),
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map http://foo.com/ http://127.0.0.1:{self._origin.port}/")
        ats.allow_private_connect()
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [{
                                "name": "common",
                                "format": '%<chi> - %<caun> [%<cqtn>] "%<cqhm> %<pqu> %<cqpv>" %<pssc> %<pscl>',
                            }],
                        "logs": [{
                            "filename": "access",
                            "format": "common"
                        }],
                    }
            })
        return ats

    def run(self) -> None:
        """Tunnel one request and validate curl diagnostics and the CONNECT log."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            f"--verbose --fail --silent --proxytunnel --proxy '127.0.0.1:{self._ats.http_port}' http://foo.com/get",
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / "connect_0_stderr.gold")
        access_log = wait_for_file_lines(self._ats.log_directory / "access.log", "CONNECT", 1)
        assert_matches_gold(access_log, TEST_DIRECTORY / "gold" / "connect_access.gold")


class VerifierConnectScenario:
    """Exercise HTTP/1.1 or HTTP/2 CONNECT with Proxy Verifier."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, *, use_http2: bool) -> None:
        self._services = services
        self._use_http2 = use_http2
        replay_name = "connect_h2.replay.yaml" if use_http2 else "connect.replay.yaml"
        self._replay = TEST_DIRECTORY / "replays" / replay_name
        suffix = "h2" if use_http2 else "h1"
        self._server = self.configure_server(services, suffix)
        self._ats = self.configure_ats(ats_factory, suffix)
        self._client = self.configure_client(services, suffix)

    def configure_server(self, services: ServiceFactory, suffix: str) -> VerifierServer:
        """Create the verifier tunnel destination."""

        return services.verifier_server(f"connect-server-{suffix}", self._replay)

    def configure_ats(self, ats_factory: ATSFactory, suffix: str) -> ATS:
        """Configure a listener and CONNECT ACL for the verifier origin."""

        ats = ats_factory.create(f"connect-ts-{suffix}", enable_tls=self._use_http2)
        if self._use_http2:
            ats.add_default_ssl_files()
            server_ports = f"{ats.https_port}:ssl"
            tags = "http|hpack"
        else:
            server_ports = str(ats.http_port)
            tags = "http|iocore_net|rec"
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": tags,
                "proxy.config.http.server_ports": server_ports,
                "proxy.config.http.connect_ports": str(self._server.http_port),
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}/")
        ats.allow_private_connect()
        return ats

    def configure_client(self, services: ServiceFactory, suffix: str) -> ProcessService:
        """Create the verifier client for the selected inbound protocol."""

        options = {"https_ports": [self._ats.https_port]} if self._use_http2 else {"http_ports": [self._ats.http_port]}
        return services.verifier_client(f"connect-client-{suffix}", self._replay, **options)

    def verify_server_output(self) -> None:
        """Require the tunneled request and exclude the CONNECT metadata at the origin."""

        if self._use_http2:
            assert "test: connect-request" not in self._server.output
            assert re.search(r"GET /get HTTP/1\.1\nuuid: 1\ntest: real-request", self._server.output)
        else:
            assert "uuid: 1" not in self._server.output
            assert re.search(r"GET /get HTTP/1\.1\nuuid: 2", self._server.output)

    def verify_metrics(self) -> None:
        """Compare the HTTP/1.1 tunnel connection metrics with their gold file."""

        gold = TEST_DIRECTORY / "gold" / "metrics.gold"
        names = [line.split()[0] for line in gold.read_text().splitlines()]
        result = self._ats.traffic_ctl("metric", "get", *names)
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stdout, gold)

    def run(self) -> None:
        """Run the tunneled verifier transaction and validate ATS accounting."""

        self._server.start()
        self._ats.start()
        self._client.run()
        self.verify_server_output()
        traffic_output = self._ats.traffic_out.read_text(errors="replace")
        assert re.search(
            rf"Proxy's Request.*\n.*\nCONNECT 127\.0\.0\.1:{self._server.http_port} HTTP/1\.1",
            traffic_output,
        )
        if not self._use_http2:
            self.verify_metrics()


def test_connect_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """curl can tunnel an HTTP request through ATS."""

    CurlConnectScenario(ats_factory, services, curl).run()


def test_connect_verifier_http1(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Proxy Verifier can carry HTTP/1.1 through an ATS CONNECT tunnel."""

    VerifierConnectScenario(ats_factory, services, use_http2=False).run()


def test_connect_verifier_http2(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Proxy Verifier can carry HTTP/1.1 inside an HTTP/2 CONNECT stream."""

    VerifierConnectScenario(ats_factory, services, use_http2=True).run()
