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

from dataclasses import dataclass
import shlex
from pathlib import Path
import os
import re
import sys

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TOOLS_DIRECTORY = TEST_DIRECTORY.parents[1] / "tools"


@dataclass(frozen=True)
class ProtocolCase:
    """Describe one client protocol used for the early-hints exchange."""

    name: str
    curl_arguments: tuple[str, ...]
    scheme: str
    enable_quic: bool = False


class EarlyHintsScenario:
    """Verify two 103 responses precede the final response on each protocol."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl
        self._dns = self.configure_dns(services)

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the synthetic backend name to loopback."""

        return services.dns("dns", default="127.0.0.1")

    @staticmethod
    def server_environment() -> dict[str, str]:
        """Expose the shared HTTP helper module to the custom origin."""

        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(TOOLS_DIRECTORY)
        return environment

    def configure_server(self, case: ProtocolCase) -> tuple[ProcessService, int]:
        """Create a one-shot origin that emits two Early Hints responses."""

        port = self._services.allocate_port()
        server = self._services.process(
            f"server_{case.name}",
            (sys.executable, TEST_DIRECTORY / "early_hints_server.py", "127.0.0.1", str(port)),
            environment=self.server_environment(),
            ready_port=port,
        )
        return server, port

    def configure_ats(self, case: ProtocolCase, server_port: int) -> ATS:
        """Create one ATS instance for @a case."""

        ats = self._ats_factory.create(
            f"ts_{case.name}",
            enable_tls=case.scheme == "https",
            enable_quic=case.enable_quic,
        )
        if case.scheme == "https":
            ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://backend.server.com:{server_port}")
        ats.records.update(
            {
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
            })
        return ats

    def run_case(self, case: ProtocolCase) -> None:
        """Run and validate one protocol exchange."""

        server, server_port = self.configure_server(case)
        ats = self.configure_ats(case, server_port)
        server.start()
        ats.start()
        port = ats.https_port if case.scheme == "https" else ats.http_port
        result = self._curl.run_for(
            ats,
            (
                f"--verbose {shlex.join(case.curl_arguments)} --resolve 'server.com:{port}:127.0.0.1' --header "
                f"'Host: server.com' '{case.scheme}://server.com:{port}/{case.name}'"),
        )
        assert result.returncode == 0, result.output
        assert re.search(r"HTTP/.* 103.*HTTP/.* 103", result.output, re.DOTALL)
        assert "ink: </style.css>; rel=preload" in result.output
        assert re.search(r"HTTP/.* 200", result.output)
        assert "10bytebody" in result.output
        server.wait(timeout=10)

    def run(self) -> None:
        """Exercise clear-text HTTP, TLS, HTTP/2, and optional HTTP/3."""

        self._dns.start()
        cases = [
            ProtocolCase("HTTP", ("--http1.1",), "http"),
            ProtocolCase("HTTPS", ("--insecure", "--http1.1"), "https"),
            ProtocolCase("HTTP2", ("--insecure", "--http2"), "https"),
        ]
        if self._ats_factory.has_feature("TS_USE_QUIC") and self._curl.supports("http3"):
            cases.append(ProtocolCase("HTTP3", ("--insecure", "--http3-only"), "https", enable_quic=True))
        for case in cases:
            self.run_case(case)


def test_early_hints(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS forwards repeated 103 Early Hints responses before the final 200."""

    EarlyHintsScenario(ats_factory, services, curl).run()
