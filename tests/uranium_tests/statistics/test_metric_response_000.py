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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(*, _services: ServiceFactory) -> OriginServer:
    """Create the origin response used by the successful control request.

    :param _services: Test-local services configured by the test.
    """

    origin = _services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(*, _ats_factory: ATSFactory, _origin: OriginServer) -> ATS:
    """Configure an uncached reverse proxy for the origin.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    ats = _ats_factory.create("ts", enable_cache=False)
    ats.records.update({
        "proxy.config.diags.debug.enabled": 0,
        "proxy.config.diags.debug.tags": "http",
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def abort_partial_request(*, _ats: ATS, _directory: Path, _services: ServiceFactory) -> None:
    """Send an incomplete request and close the connection.

    :param _ats: Test-local ats configured by the test.
    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    """

    client = _services.process(
        "abort-client",
        [sys.executable, _directory / "abort_client.py", "127.0.0.1",
         str(_ats.http_port)],
    )
    client.run()


def send_control_request(*, _ats: ATS, _curl: Curl) -> None:
    """Verify an ordinary completed transaction still succeeds.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.get(_ats, options=f"-s -o /dev/null -w '%{{http_code}}'")
    assert result.returncode == 0, result.output
    assert result.stdout == "200"


def verify_metric(*, _ats: ATS) -> None:
    """Wait until ATS publishes exactly one 000 response.

    :param _ats: Test-local ats configured by the test.
    """

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = _ats.traffic_ctl("metric", "get", "proxy.process.http.000_responses")
        if result.returncode == 0 and result.stdout.rstrip().endswith(" 1"):
            return
        time.sleep(0.1)
    raise AssertionError(f"000-response metric did not reach one:\n{result.output}")


def test_metric_response_000(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Client aborts increment proxy.process.http.000_responses.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _directory = Path(__file__).parent
    _origin = configure_origin(_services=services)
    _ats = configure_ats(_ats_factory=ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    abort_partial_request(_ats=_ats, _directory=_directory, _services=services)
    send_control_request(_ats=_ats, _curl=curl)
    verify_metric(_ats=_ats)
