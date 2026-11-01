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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

TSAPI__plugin = "test_tsapi.so"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve the root and mixed-case path transactions.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    for path, body in (("/", "112233"), ("/xYz", "445566")):
        origin.add_response(
            {
                "headers": f"GET {path} HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": body
            },
        )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Load two instances of the API plugin on HTTP and HTTPS maps.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/pluginTest/tsapi/.libs/test_tsapi.so")
    ats.set_environment("OUTPUT_FILE", str(ats.log_directory / "log.txt"))
    ats.records.update(
        {
            "proxy.config.proxy_name": "Poxy_Proxy",
            "proxy.config.url_remap.remap_required": 1,
            "proxy.config.diags.debug.enabled": 3,
            "proxy.config.diags.debug.tags": "http|test_tsapi",
        })
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    plugins = f"@plugin={TSAPI__plugin} @plugin={TSAPI__plugin}"
    ats.remap_config.add_lines(
        (
            f"map http://myhost.test http://127.0.0.1:{_origin.port} {plugins}",
            f"map https://myhost.test:123 http://127.0.0.1:{_origin.port} {plugins}",
        ))
    return ats


def require_request(*arguments: str, _ats: ATS, _curl: Curl) -> None:
    """Run a curl request and require an origin success response.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _curl.run_for(
        _ats,
        f"--verbose {shlex.join(arguments)}",
    )
    assert result.returncode == 0, result.output
    assert "200 OK" in result.stderr or "HTTP/2 200" in result.stderr


def test_tsapi(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The TS URL APIs preserve raw and normalized request components.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _log = _ats.log_directory / "log.txt"

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _origin.start()
    _ats.start()
    require_request("--ipv4", "--header", "Host: mYhOsT.teSt", f"hTtP://loCalhOst:{_ats.http_port}/", _ats=_ats, _curl=curl)
    require_request("--ipv4", "--proxy", f"localhost:{_ats.http_port}", "http://mYhOsT.teSt/xYz", _ats=_ats, _curl=curl)
    if not curl.uses_uds:
        require_request(
            "--ipv4",
            "--http2",
            "--insecure",
            "--header",
            "Host: myhost.test:123",
            f"HttPs://LocalHost:{_ats.https_port}/",
            _ats=_ats,
            _curl=curl)

    expected_events = 2 if curl.uses_uds else 3
    log = wait_for_file_lines(_log, "Global: event=TS_EVENT_HTTP_SEND_REQUEST_HDR", expected_events)
    normalized = log.replace(str(_origin.port), "SERVER_PORT")
    gold = "log_uds.gold" if curl.uses_uds else "log.gold"
    assert_matches_gold(normalized, TEST_DIRECTORY / gold)
