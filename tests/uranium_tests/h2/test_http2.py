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
"""Cover HTTP/2 behavior that requires curl or bespoke frame clients."""

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def http2_tls_configure_origin(services: ServiceFactory, name: str) -> OriginServer:
    """Create a default empty HTTP response origin.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    """

    origin = services.origin(f"origin-{name}")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"},
    )
    return origin


def http2_tls_configure_ats(ats_factory: ATSFactory, name: str, *, _origin: OriginServer) -> ATS:
    """Configure a TLS HTTP/2 ingress mapped to the origin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    """

    ats = ats_factory.create(f"ats-{name}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http2.active_timeout_in": 3,
            "proxy.config.http2.max_concurrent_streams_in": 65535,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def http2_tls_start(*, _ats: ATS, _origin: OriginServer) -> None:
    """Start the origin and ATS in dependency order.

    :param _ats: Test-local ats configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    _origin.start()
    _ats.start()


def http2_settings_rate_limit_configure_ats(ats_factory: ATSFactory) -> ATS:
    """Disable per-frame limiting and allow one setting change per minute.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats-settings-rate-limit", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http2.max_settings_per_frame": -1,
            "proxy.config.http2.max_settings_per_minute": 1,
            "proxy.config.http2.max_settings_frames_per_minute": 100,
        })
    return ats


HTTP2_CURL_LARGE_BODY = "0123456789" * 131070

HTTP2_CURL_SMALL_BODY = "1234567890" * 11


def http2_curl_configure_origin(services: ServiceFactory) -> OriginServer:
    """Create responses for small upload, large upload, and huge headers.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin-curl")
    for path, body in (("/postchunked", HTTP2_CURL_SMALL_BODY), ("/bigpostchunked", HTTP2_CURL_LARGE_BODY)):
        origin.add_response(
            {
                "headers": f"POST {path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": body
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nContent-Length: 10\r\n\r\n",
                "body": "0123456789",
            },
        )
    origin.add_response(
        {"headers": "GET /huge_resp_hdrs HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nContent-Length: 6\r\n\r\n",
            "body": "200 OK",
        },
    )
    return origin


def http2_curl_configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure TLS and header_rewrite for the large response-header case.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats-curl", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    if not ats.plugin_exists("header_rewrite.so"):
        pytest.skip("header_rewrite.so is not installed")
    ats.copy_to_config(TEST_DIRECTORY / "rules" / "huge_resp_hdrs.conf")
    ats.remap_config.add_lines(
        (
            f"map /huge_resp_hdrs http://127.0.0.1:{_origin.port}/huge_resp_hdrs "
            f"@plugin=header_rewrite.so @pparam={ats.config_directory / 'huge_resp_hdrs.conf'}",
            f"map / http://127.0.0.1:{_origin.port}",
        ))
    return ats


def http2_curl_post(path: str, data: str, *, _ats: ATS, _curl: Curl) -> None:
    """POST through curl's chunked-input mode and verify the response.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param data: Data used by this test step.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--silent --show-error --insecure --header 'Transfer-Encoding: chunked' --data-binary '{data}' "
            f"'https://127.0.0.1:{_ats.https_port}{path}'"),
        timeout=30,
    )
    assert result.returncode == 0, result.output
    assert result.stdout == "0123456789", result.output


def http2_curl_verify_huge_response_headers(*, _ats: ATS, _curl: Curl) -> None:
    """Verify six large fields survive HTTP/2 header encoding and decoding.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        f"--verbose --silent --insecure --http2 'https://127.0.0.1:{_ats.https_port}/huge_resp_hdrs'",
        timeout=30,
    )
    assert result.returncode == 0, result.output
    assert result.stdout == "200 OK", result.output
    assert "HTTP/2 200" in result.stderr, result.output
    for index in range(6):
        assert f"x-huge-{index}:" in result.stderr.lower(), result.output


def test_http2_active_timeout(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The custom client observes the configured H2 active timeout.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    name = "active-timeout"
    _origin = http2_tls_configure_origin(services, name)
    _ats = http2_tls_configure_ats(ats_factory, name, _origin=_origin)

    http2_tls_start(_ats=_ats, _origin=_origin)
    result = _ats.run(
        sys.executable,
        TEST_DIRECTORY / "h2active_timeout.py",
        str(_ats.https_port),
        "/",
        "4",
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "CONNECTION_TIMEOUT" in result.output


def test_http2_extension_settings(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """An extension setting does not prevent the following request.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    name = "extension-settings"
    _origin = http2_tls_configure_origin(services, name)
    _ats = http2_tls_configure_ats(ats_factory, name, _origin=_origin)

    http2_tls_start(_ats=_ats, _origin=_origin)
    result = _ats.run(
        sys.executable,
        TEST_DIRECTORY / "clients" / "h2_extension_settings.py",
        str(_ats.https_port),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "Received 200 response" in result.stdout


def test_http2_settings_rate_limit(ats_factory: ATSFactory) -> None:
    """Frequent setting changes trigger ENHANCE_YOUR_CALM.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = http2_settings_rate_limit_configure_ats(ats_factory)

    _ats.start()
    result = _ats.run(
        sys.executable,
        TEST_DIRECTORY / "clients" / "h2_max_settings_per_minute.py",
        str(_ats.https_port),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "Received GOAWAY with error code 11" in result.stdout
    wait_for_file_lines(
        _ats.diags_log,
        r"ERROR: HTTP/2 connection error.*recv settings too frequent setting changes",
        1,
    )


def test_http2_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Curl can upload chunked bodies and receive huge HTTP/2 fields.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("TLS HTTP/2 curl coverage requires a TCP listener")
    if not curl.supports("http2"):
        pytest.skip("curl does not support HTTP/2")
    _origin = http2_curl_configure_origin(services)
    _ats = http2_curl_configure_ats(ats_factory, _origin=_origin)
    _large_body_file = ats_factory.run_directory / "big_post_body"
    _large_body_file.write_text(HTTP2_CURL_LARGE_BODY)

    _origin.start()
    _ats.start()
    http2_curl_post("/postchunked", HTTP2_CURL_SMALL_BODY, _ats=_ats, _curl=curl)
    http2_curl_post("/bigpostchunked", f"@{_large_body_file}", _ats=_ats, _curl=curl)
    http2_curl_verify_huge_response_headers(_ats=_ats, _curl=curl)
