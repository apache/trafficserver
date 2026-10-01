#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import subprocess
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

H3_RANGE_CACHE__range_body = "6789012345678901"

H3_RANGE_CACHE__response_body = "0123456789" * 30000


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the cacheable large response.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server-h3-range-cache")
    origin.add_response(
        {"headers": "GET /h3-range-cache HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: public, max-age=60\r\n"
                    f"Content-Length: {len(H3_RANGE_CACHE__response_body)}\r\n\r\n"),
            "body": H3_RANGE_CACHE__response_body,
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure cached HTTP/3 ingress.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-h3-range-cache", enable_tls=True, enable_quic=True, enable_cache=True)
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic|http3|http",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def request(output: str, *extra: str, _ats: ATS, _curl: Curl) -> str:
    """Run a common HTTP/3 request and return curl's write-out text.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param output: Output used by this test step.
    :param extra: Extra used by this test step.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--silent --show-error --fail --ipv4 --http3-only --insecure --resolve "
            f"'range.example.com:{_ats.https_port}:127.0.0.1' {shlex.join(extra)} --output '{output}' "
            f"--write-out '\nhttp_code=%{{http_code}}\nsize_download=%{{size_download}}\n' "
            f"'https://range.example.com:{_ats.https_port}/h3-range-cache'"),
        timeout=30,
    )
    assert result.returncode == 0, result.output
    return result.stdout


def test_h3_range_cache(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP/3 range requests are served correctly from cached content.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    curl_help = subprocess.check_output(("curl", "--help", "all"), text=True)
    if not ats_factory.has_feature("TS_USE_QUIC") or not Curl.supports("http3") or "--http3-only" not in curl_help:
        pytest.skip("ATS QUIC and curl HTTP/3 support are required")
    if curl.uses_uds:
        pytest.skip("HTTP/3 requires a UDP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    sandbox = _ats.run_directory.parent
    full_body = sandbox / "h3-range-full.txt"
    fill = request(str(full_body), _ats=_ats, _curl=curl)
    assert "http_code=200" in fill
    assert f"size_download={len(H3_RANGE_CACHE__response_body)}" in fill
    assert full_body.read_text() == H3_RANGE_CACHE__response_body

    range_body = sandbox / "h3-range-part.txt"
    ranged = request(str(range_body), "--header", "Range: bytes=16-31", _ats=_ats, _curl=curl)
    assert "http_code=206" in ranged
    assert "size_download=16" in ranged
    assert range_body.read_text() == H3_RANGE_CACHE__range_body
