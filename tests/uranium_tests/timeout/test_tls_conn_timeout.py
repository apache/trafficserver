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
        """Path."""
        delay = "connect" if self.handshake_delay else "ttfb"
        return f"/{self.method.lower()}_{delay}_blocked"


CASES = (
    TimeoutCase("POST", 3, 0, "HTTP/1.1 502 Connection timed out"),
    TimeoutCase("POST", 0, 6, "504 Connection Timed Out"),
    TimeoutCase("GET", 3, 0, "HTTP/1.1 502 Connection timed out"),
    TimeoutCase("GET", 0, 6, "504 Connection Timed Out"),
)


def configure_origin(
        context: ProceduralContext, services: ServiceFactory, *, _case: TimeoutCase, _origin_port: int) -> ProcessService:
    """Start the compiled TLS server with the requested delay points.

    :param _case: Test-local case configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param context: Context used by this test step.
    :param services: Factory owning support services and their cleanup.
    """

    binary = context.runtime.resolve_artifact(context.test_directory, "{AtsBuildUraniumTestsDir}/timeout/ssl-delay-server")
    certificate = context.runtime.test_tools / "ssl" / "server.pem"
    return services.process(
        "origin",
        (
            binary,
            str(_origin_port),
            str(_case.handshake_delay),
            str(_case.response_delay),
            certificate,
        ),
        ready_port=_origin_port,
    )


def configure_ats(ats_factory: ATSFactory, *, _case: TimeoutCase, _origin_port: int) -> ATS:
    """Apply distinct handshake and transaction timeout limits.

    :param _case: Test-local case configured by the test.
    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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
    ats.remap_config.add_line(f"map {_case.path} https://127.0.0.1:{_origin_port}")
    return ats


def run_client(*, _ats: ATS, _case: TimeoutCase, _curl: Curl) -> CommandResult:
    """Issue the case's GET or POST request.

    :param _ats: Test-local ats configured by the test.
    :param _case: Test-local case configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    arguments = ["--header", "Connection: close", "--include", "--tlsv1.2"]
    if _case.method == "POST":
        arguments.extend(("--data", "bob"))
    arguments.append(f"http://127.0.0.1:{_ats.http_port}{_case.path}")
    return _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=20,
    )


def verify(result: CommandResult, *, _case: TimeoutCase, _origin: ProcessService) -> None:
    """Require the expected proxy status and origin delay path.

    :param _case: Test-local case configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert _case.expected_status in result.output
    assert "Accept try" in _origin.output
    if _case.response_delay:
        assert "TTFB delay" in _origin.output
    else:
        assert "TTFB delay" not in _origin.output


@pytest.mark.parametrize("case", CASES, ids=("post-handshake", "post-ttfb", "get-handshake", "get-ttfb"))
def test_tls_conn_timeout(
    case: TimeoutCase,
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
) -> None:
    """ATS distinguishes TLS handshake timeouts from response timeouts.

    :param case: Case used by this test step.
    :param procedural_context: Procedural context used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    context = procedural_context
    _origin_port = services.allocate_port()
    _origin = configure_origin(context, services, _case=case, _origin_port=_origin_port)
    _ats = configure_ats(ats_factory, _case=case, _origin_port=_origin_port)

    _origin.start()
    _ats.start()
    verify(run_client(_ats=_ats, _case=case, _curl=curl), _case=case, _origin=_origin)
