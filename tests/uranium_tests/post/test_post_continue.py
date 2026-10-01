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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer

POST_CONTINUE__replay = "replay/post-continue.replay.yaml"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the verifier origin that accepts the repeated POST requests.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("server", POST_CONTINUE__replay)


def configure_ats(ats_factory: ATSFactory, name: str, *, send_immediately: bool, _server: VerifierServer) -> ATS:
    """Configure one ATS instance's 100-continue response policy.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param send_immediately: Send immediately used by this test step.
    """

    ats = ats_factory.create(name, enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.send_100_continue_response": int(send_immediately),
        })
    return ats


def run_case(ats: ATS, protocol: str, body: str | Path, *, expect_continue: bool, _curl: Curl) -> None:
    """Run and verify one member of the protocol, size, and policy matrix.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param protocol: Protocol variant exercised by the test.
    :param body: HTTP message body.
    :param expect_continue: Expect continue used by this test step.
    """

    expect_header = "Expect: 100-continue" if expect_continue else "Expect:"
    result = _curl.run_for(
        ats,
        (
            f"--verbose --output /dev/null '--{protocol}' --header 'uuid: post' --header '{expect_header}' --data "
            f"'{f'@{body}' if isinstance(body, Path) else body}' --insecure "
            f"'https://127.0.0.1:{ats.https_port}/post'"),
        timeout=30,
    )
    assert result.returncode == 0, result.output
    if protocol == "http2":
        assert "POST /post HTTP/2" in result.output
        assert "HTTP/2 200" in result.output
        continue_response = "HTTP/2 100"
    else:
        assert "> POST /post HTTP/1.1" in result.output
        assert "< HTTP/1.1 200 OK" in result.output
        continue_response = "HTTP/1.1 100"
    if expect_continue:
        assert "xpect: 100-continue" in result.output
        assert continue_response in result.output
    else:
        assert "xpect: 100-continue" not in result.output
        assert continue_response not in result.output


def test_post_continue(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS handles Expect: 100-continue across body sizes and HTTP versions.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("the TLS protocol matrix requires TCP listeners")
    if not Curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _server = configure_server(services)
    _delayed = configure_ats(ats_factory, "ts", send_immediately=False, _server=_server)
    _immediate = configure_ats(ats_factory, "ts2", send_immediately=True, _server=_server)

    _server.start()
    _delayed.start()
    _immediate.start()
    large_body = _delayed.run_directory.parent / "big_post_body"
    large_body.write_text("0123456789" * 131070)
    for ats in (_delayed, _immediate):
        for protocol in ("http1.1", "http2"):
            for body in ("small body", large_body):
                for expect_continue in (True, False):
                    run_case(ats, protocol, body, expect_continue=expect_continue, _curl=curl)
