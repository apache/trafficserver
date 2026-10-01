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
import shlex

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the reusable empty-response origin.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure TLS access logging and the pre-accept hook plugin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.ssl.TLSv1_3.enabled": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map https://example.com:{ats.https_port} http://127.0.0.1:{_origin.port}")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "testformat",
                        "format": "%<cqssl> %<cqtr>"
                    }],
                    "logs": [{
                        "mode": "ascii",
                        "format": "testformat",
                        "filename": "squid"
                    }],
                }
        })
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
    ats.plugin_config.add_line("ssl_secret_load_test.so")
    return ats


def curl_arguments(protocol: str, *, _ats: ATS) -> tuple[str, ...]:
    """Return common curl arguments for @a protocol.

    :param _ats: Test-local ats configured by the test.
    :param protocol: Protocol variant exercised by the test.
    """

    return (
        "--insecure",
        "--verbose",
        f"--{protocol}",
        "--header",
        f"host:example.com:{_ats.https_port}",
    )


def request_pair(protocol: str, *, same_connection: bool, _ats: ATS, _curl: Curl) -> None:
    """Issue two requests on either one or two client connections.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param protocol: Protocol variant exercised by the test.
    :param same_connection: Same connection used by this test step.
    """

    url = f"https://127.0.0.1:{_ats.https_port}"
    arguments = curl_arguments(protocol, _ats=_ats)
    if same_connection:
        result = _curl.run_for(
            _ats,
            f"{shlex.join(arguments)} '{url}' '{url}'",
        )
        assert result.returncode == 0, result.output
    else:
        for _ in range(2):
            result = _curl.run_for(
                _ats,
                f"{shlex.join(arguments)} '{url}'",
            )
            assert result.returncode == 0, result.output


def test_tls_keepalive(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """TLS keep-alive is honored for HTTP/1.1 and HTTP/2 clients.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("TLS keep-alive coverage requires a TCP listener")
    if not Curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    request_pair("http1.1", same_connection=True, _ats=_ats, _curl=curl)
    request_pair("http1.1", same_connection=False, _ats=_ats, _curl=curl)
    request_pair("http2", same_connection=True, _ats=_ats, _curl=curl)
    request_pair("http2", same_connection=False, _ats=_ats, _curl=curl)
    access_log = _ats.log_directory / "squid.log"
    content = wait_for_file_lines(access_log, r"^1 [01]$", 8, timeout=10)
    assert_matches_gold(content, TEST_DIRECTORY / "gold" / "accesslog.gold")
