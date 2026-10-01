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

import pytest

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> HttpBinServer:
    """Serve the generic resources requested by h2spec.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("httpbin")


def configure_ats(ats_factory: ATSFactory, *, _origin: HttpBinServer) -> ATS:
    """Expose an uncached TLS endpoint with Via headers enabled.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.http.insert_request_via_str": 1,
            "proxy.config.http.insert_response_via_str": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Select the generic, framing, stream, and HPACK conformance groups.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    targets = ("generic", "http2/3", "http2/4", "http2/5", "http2/6", "http2/7", "http2/8", "hpack")
    return services.process(
        "h2spec",
        ("h2spec", *targets, "-t", "-k", "--timeout", "10", "-p", str(_ats.https_port)),
    )


def test_h2spec(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS passes the selected h2spec conformance groups.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    if shutil.which("h2spec") is None:
        pytest.skip("h2spec is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=120)
    assert_matches_gold(result.stdout, TEST_DIRECTORY / "gold" / "h2spec_stdout.gold")
    assert "ERROR: HTTP/2" in _ats.diags_log.read_text(errors="replace")
