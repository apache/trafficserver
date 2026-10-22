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

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> HttpBinServer:
    """Create an origin that serves the cacheable response.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("httpbin")


def configure_ats(ats_factory: ATSFactory, *, _origin: HttpBinServer) -> ATS:
    """Configure HTTP/2 error-rate accounting and the origin remap.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=True)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2",
            "proxy.config.http.insert_response_via_str": 2,
            "proxy.config.http2.active_timeout_in": 3,
            "proxy.config.http2.stream_error_rate_threshold": 0.1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def run_client(name: str, streams: int, *, _ats: ATS, _client: Path, _services: ServiceFactory) -> None:
    """Send empty DATA frames on @a streams streams.

    :param _ats: Test-local ats configured by the test.
    :param _client: Test-local client configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param streams: Streams used by this test step.
    """

    result = _services.process(
        name,
        [sys.executable, _client, str(_ats.https_port), "/cache/10", "-n",
         str(streams)],
    ).run()
    assert result.returncode == 0, result.output


def test_http2_empty_data_frame(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Empty end-of-stream DATA frames are not counted as stream errors.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _client = Path(__file__).parent / "clients" / "h2empty_data_frame.py"
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    run_client("warm-cache", 1, _ats=_ats, _client=_client, _services=services)
    run_client("twenty-streams", 20, _ats=_ats, _client=_client, _services=services)
    assert _ats.is_running
