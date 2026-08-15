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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ProceduralContext, ProcessService, ServiceFactory


@dataclass(frozen=True)
class TimeoutCase:
    """Describe one delayed TLS-origin request."""

    method: str
    handshake_delay: int
    response_delay: int
    expected_status: str

    @property
    def path(self) -> str:
        delay = "connect" if self.handshake_delay else "ttfb"
        return f"/{self.method.lower()}_{delay}_blocked"


CASES = (
    TimeoutCase("POST", 3, 0, "HTTP/1.1 502 Connection timed out"),
    TimeoutCase("POST", 0, 6, "504 Connection Timed Out"),
    TimeoutCase("GET", 3, 0, "HTTP/1.1 502 Connection timed out"),
    TimeoutCase("GET", 0, 6, "504 Connection Timed Out"),
)


class TlsOriginTimeoutScenario:
    """Delay either a TLS handshake or the first origin response byte."""

    def __init__(
        self,
        case: TimeoutCase,
        context: ProceduralContext,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
    ) -> None:
        self._case = case
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(context, services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, context: ProceduralContext, services: ServiceFactory) -> ProcessService:
        """Start the compiled TLS server with the requested delay points."""

        binary = context.runtime.resolve_artifact(context.test_directory, "{AtsBuildUraniumTestsDir}/timeout/ssl-delay-server")
        certificate = context.runtime.test_tools / "ssl" / "server.pem"
        return services.process(
            "origin",
            (
                binary,
                str(self._origin_port),
                str(self._case.handshake_delay),
                str(self._case.response_delay),
                certificate,
            ),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Apply distinct handshake and transaction timeout limits."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.url_remap.remap_required": 1,
                "proxy.config.http.connect_attempts_timeout": 1,
                "proxy.config.http.connect_attempts_max_retries": 1,
                "proxy.config.http.transaction_no_activity_timeout_out": 4,
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http|ssl",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_line(f"map {self._case.path} https://127.0.0.1:{self._origin_port}")
        return ats

    def run_client(self) -> CommandResult:
        """Issue the case's GET or POST request."""

        arguments = ["--header", "Connection: close", "--include", "--tlsv1.2"]
        if self._case.method == "POST":
            arguments.extend(("--data", "bob"))
        arguments.append(f"http://127.0.0.1:{self._ats.http_port}{self._case.path}")
        return self._curl.run_for(self._ats, *arguments, timeout=20)

    def verify(self, result: CommandResult) -> None:
        """Require the expected proxy status and origin delay path."""

        assert result.returncode == 0, result.output
        assert self._case.expected_status in result.output
        assert "Accept try" in self._origin.output
        if self._case.response_delay:
            assert "TTFB delay" in self._origin.output
        else:
            assert "TTFB delay" not in self._origin.output

    def run(self) -> None:
        """Start the delayed origin and ATS, then verify the request."""

        self._origin.start()
        self._ats.start()
        self.verify(self.run_client())


@pytest.mark.parametrize("case", CASES, ids=("post-handshake", "post-ttfb", "get-handshake", "get-ttfb"))
def test_tls_conn_timeout(
    case: TimeoutCase,
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """ATS distinguishes TLS handshake timeouts from response timeouts."""

    TlsOriginTimeoutScenario(case, procedural_context, ats_factory, services, curl).run()
