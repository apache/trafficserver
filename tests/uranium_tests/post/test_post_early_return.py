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
import shutil
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origins(services: ServiceFactory, *, _ports: list[int]) -> list[ProcessService]:
    """Create one single-use early-response origin for each transaction.

    :param _ports: Test-local ports configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    mock_origin = TEST_DIRECTORY.parents[1] / "tools" / "mock_origin.py"
    origins = []
    for number, port in enumerate(_ports, 1):
        origins.append(
            services.process(
                f"server{number}",
                (
                    sys.executable,
                    mock_origin,
                    str(port),
                    "--status",
                    "420",
                    "--reason",
                    "Be Calm",
                    "--output",
                    f"outserver{number}",
                ),
                ready_port=port,
            ))
    return origins


def configure_ats(ats_factory: ATSFactory, *, _ports: list[int]) -> ATS:
    """Configure TLS and route each case to its single-use origin.

    :param _ports: Test-local ports configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update({
        "proxy.config.diags.debug.enabled": 0,
        "proxy.config.diags.debug.tags": "http",
    })
    for name, port in zip(("one", "two", "three", "four", "five", "six"), _ports):
        ats.remap_config.add_line(f"map /{name} http://127.0.0.1:{port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    return ats


def run_curl_case(protocol: str, path: str, body: str, *, _ats: ATS, _curl: Curl) -> None:
    """POST @a body with curl and require the early origin response.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param protocol: Protocol variant exercised by the test.
    :param path: Resource or file path used by this operation.
    :param body: HTTP message body.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--verbose --output /dev/null '--{protocol}' --header Expect: --data '{body}' --insecure "
            f"'https://127.0.0.1:{_ats.https_port}/{path}'"),
        timeout=30,
    )
    assert result.returncode == 0, result.output
    expected = "HTTP/2 420" if protocol == "http2" else "HTTP/1.1 420 Be Calm"
    assert expected in result.output


def run_delayed_case(number: int, output_name: str, *, _ats: ATS, _services: ServiceFactory) -> None:
    """Run one raw client that pauses before completing its request body.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param number: Number used by this test step.
    :param output_name: Output name used by this test step.
    """

    output = _ats.run_directory.parent / output_name
    suffix = "" if number == 1 else str(number)
    client = _services.process(
        f"client{number}",
        (
            "sh",
            TEST_DIRECTORY / f"delay_client{suffix}.sh",
            str(_ats.http_port),
            output,
        ),
    )
    result = client.run(timeout=15)
    assert result.returncode == 0, result.output
    response = output.read_text(errors="replace")
    assert "0123456789" not in response
    assert "HTTP/1.1 420 Be Calm" in response
    assert "Connection: close" in response


def test_post_early_return(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns an early origin response without forwarding the remaining body.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("the raw delayed clients require a TCP listener")
    if not Curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    if shutil.which("nc") is None:
        pytest.skip("nc is required for the delayed POST clients")
    _ports = [services.allocate_port() for _ in range(6)]
    _origins = configure_origins(services, _ports=_ports)
    _ats = configure_ats(ats_factory, _ports=_ports)

    for origin in _origins:
        origin.start()
    _ats.start()
    body = _ats.run_directory.parent / "big_post_body"
    body.write_text("0123456789" * 231070)

    run_curl_case("http1.1", "one", "small body", _ats=_ats, _curl=curl)
    run_curl_case("http1.1", "two", f"@{body}", _ats=_ats, _curl=curl)
    run_curl_case("http2", "three", f"@{body}", _ats=_ats, _curl=curl)
    run_delayed_case(1, "clientout", _ats=_ats, _services=services)
    run_delayed_case(2, "clientout2", _ats=_ats, _services=services)
    run_delayed_case(3, "clientout3", _ats=_ats, _services=services)
