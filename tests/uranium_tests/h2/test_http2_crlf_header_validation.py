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

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory

INVALID_H2_HEADER_CASES = (
    "crlf-in-header-value",
    "cr-in-header-value",
    "lf-in-header-value",
    "nul-in-header-value",
)


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create an origin used to detect accidentally forwarded requests.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure a TLS HTTP/2 listener in front of the origin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http",
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def run_client(scenario: str, *, _ats: ATS, _client: Path, _services: ServiceFactory) -> None:
    """Run one raw-wire malformed-header case.

    :param _ats: Test-local ats configured by the test.
    :param _client: Test-local client configured by the test.
    :param _services: Test-local services configured by the test.
    :param scenario: Scenario used by this test step.
    """

    result = _services.process(
        f"client-{scenario}",
        [sys.executable, _client, str(_ats.https_port), scenario],
    ).run()
    assert re.search(r"Received (RST_STREAM|GOAWAY|HTTP/2 response with status 4\d\d)", result.stdout), result.output


def test_http2_crlf_header_validation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HTTP/2 requests with NUL, CR, or LF header values are rejected.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _client = Path(__file__).parent.parent / "connect" / "malformed_h2_request_client.py"
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    for scenario in INVALID_H2_HEADER_CASES:
        run_client(scenario, _ats=_ats, _client=_client, _services=services)
    assert "x-injected" not in _origin.output
    assert "malformed-nul-value" not in _origin.output
