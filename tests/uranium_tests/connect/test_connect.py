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

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    HttpBinServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


def curl_connect_configure_origin(services: ServiceFactory) -> HttpBinServer:
    """Create the HTTP origin reached after CONNECT succeeds.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("httpbin")


def curl_connect_configure_ats(ats_factory: ATSFactory, *, _origin: HttpBinServer) -> ATS:
    """Allow CONNECT only to the allocated origin port.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.server_ports": str(ats.http_port),
            "proxy.config.http.connect_ports": str(_origin.port),
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map http://foo.com/ http://127.0.0.1:{_origin.port}/")
    ats.allow_private_connect()
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "common",
                        "format": '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl>',
                    }],
                    "logs": [{
                        "filename": "access",
                        "format": "common"
                    }],
                }
        })
    return ats


def test_connect_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """curl can tunnel an HTTP request through ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = curl_connect_configure_origin(services)
    _ats = curl_connect_configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.run_for(
        _ats,
        f"--verbose --fail --silent --proxytunnel --proxy '127.0.0.1:{_ats.http_port}' http://foo.com/get",
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert re.search(rf"(Connected to|Established connection to) 127\.0\.0\.1.*{_ats.http_port}", result.stderr)
    assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / "connect_0_stderr.gold")
    access_log = wait_for_file_lines(_ats.log_directory / "access.log", "CONNECT", 1)
    assert_matches_gold(access_log, TEST_DIRECTORY / "gold" / "connect_access.gold")
