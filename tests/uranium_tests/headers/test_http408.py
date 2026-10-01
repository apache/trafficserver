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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent
TCP_CLIENT = TEST_DIRECTORY.parents[1] / "tools" / "tcp_client.py"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Provide the mapped origin, which the incomplete request never reaches.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.http408.test\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": ""
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Use a short inbound transaction inactivity timeout.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.remap_config.add_line(f"map http://www.http408.test http://127.0.0.1:{_origin.port}")
    ats.records.update({"proxy.config.http.transaction_no_activity_timeout_in": 2})
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Use the raw client so the declared body remains unfinished.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "timeout-client",
        (
            sys.executable,
            TCP_CLIENT,
            "127.0.0.1",
            str(_ats.http_port),
            TEST_DIRECTORY / "data" / "www.http408.test.txt",
            "--delay-after-send",
            "4",
        ),
    )


def test_http408(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An incomplete request receives ATS's standard 408 response.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    if curl.uses_uds:
        pytest.skip("the raw TCP client requires a TCP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=10)
    assert_matches_gold(result.stdout, TEST_DIRECTORY / "http408.gold")
