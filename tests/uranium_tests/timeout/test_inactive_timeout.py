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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Delay the origin response long enough for ATS to time out.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", delay=8)
    origin.add_response(
        {
            "headers": "GET /file HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": ""
        },
        "sessionfile.log",
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Enable HTTP and TLS listeners with a two-second origin timeout.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.records.update({
        "proxy.config.url_remap.remap_required": 1,
        "proxy.config.http.transaction_no_activity_timeout_out": 2,
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    return ats


def verify_timeout(result: CommandResult) -> None:
    """Require the ATS timeout response body.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert "Inactivity Timeout" in result.stdout


def test_inactive_timeout(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns an inactivity timeout for stalled origin responses.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    verify_timeout(curl.get(_ats, "/file", options=f"--include", timeout=20))
    if curl.uses_uds:
        return
    for protocol in ("--http1.1", "--http2"):
        result = curl.run(
            f"--insecure --include '{protocol}' 'https://127.0.0.1:{_ats.https_port}/file'",
            timeout=20,
        )
        verify_timeout(result)
