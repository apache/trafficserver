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


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the synthetic backend name to loopback.

    :param services: Factory owning support services and their cleanup.
    """

    return services.dns("dns", default="127.0.0.1")


def server_environment() -> dict[str, str]:
    """Expose the shared HTTP helper module to the custom origin."""

    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(TOOLS_DIRECTORY)
    return environment


def configure_server(case: ProtocolCase, *, _services: ServiceFactory) -> tuple[ProcessService, int]:
    """Create a one-shot origin that emits two Early Hints responses.

    :param _services: Test-local services configured by the test.
    :param case: Case used by this test step.
    """

    port = _services.allocate_port()
    server = _services.process(
        f"server_{case.name}",
        (sys.executable, TEST_DIRECTORY / "early_hints_server.py", "127.0.0.1", str(port)),
        environment=server_environment(),
        ready_port=port,
    )
    return server, port


def configure_ats(case: ProtocolCase, server_port: int, *, _ats_factory: ATSFactory, _dns: DNSServer) -> ATS:
    """Create one ATS instance for @a case.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param case: Case used by this test step.
    :param server_port: Allocated server listener port number.
    """

    ats = _ats_factory.create(
        f"ts_{case.name}",
        enable_tls=case.scheme == "https",
        enable_quic=case.enable_quic,
    )
    if case.scheme == "https":
        ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://backend.server.com:{server_port}")
    ats.records.update(
        {
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
    return ats


def run_case(case: ProtocolCase, *, _ats_factory: ATSFactory, _curl: Curl, _dns: DNSServer, _services: ServiceFactory) -> None:
    """Run and validate one protocol exchange.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _services: Test-local services configured by the test.
    :param case: Case used by this test step.
    """

    server, server_port = configure_server(case, _services=_services)
    ats = configure_ats(case, server_port, _ats_factory=_ats_factory, _dns=_dns)
    server.start()
    ats.start()
    port = ats.https_port if case.scheme == "https" else ats.http_port
    result = _curl.run_for(
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


def test_early_hints(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS forwards repeated 103 Early Hints responses before the final 200.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _dns = configure_dns(services)

    _dns.start()
    cases = [
        ProtocolCase("HTTP", ("--http1.1",), "http"),
        ProtocolCase("HTTPS", ("--insecure", "--http1.1"), "https"),
        ProtocolCase("HTTP2", ("--insecure", "--http2"), "https"),
    ]
    if ats_factory.has_feature("TS_USE_QUIC") and curl.supports("http3"):
        cases.append(ProtocolCase("HTTP3", ("--insecure", "--http3-only"), "https", enable_quic=True))
    for case in cases:
        run_case(case, _ats_factory=ats_factory, _curl=curl, _dns=_dns, _services=services)
