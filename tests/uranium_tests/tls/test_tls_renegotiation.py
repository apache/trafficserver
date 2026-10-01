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
import ssl
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the normal response used after renegotiation is refused.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "ok"
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Disable client renegotiation while retaining TLS 1.2.

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
            "proxy.config.ssl.allow_client_renegotiation": 0,
            "proxy.config.ssl.TLSv1_3.enabled": 0,
            "proxy.config.ssl.TLSv1_2": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_load",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the TLS 1.2 client that requests renegotiation.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "renegotiation-client",
        (
            sys.executable,
            TEST_DIRECTORY / "tls_renegotiation_client.py",
            "-p",
            str(_ats.https_port),
            "-s",
            "example.com",
        ),
    )


def test_tls_renegotiation(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Disallowed client renegotiation is refused without taking down ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    version = re.search(r"\d+(?:\.\d+)+", ssl.OPENSSL_VERSION)
    if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    if curl.uses_uds:
        pytest.skip("TLS renegotiation requires a TCP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    renegotiation = _client.run(timeout=20)
    assert renegotiation.returncode == 0, renegotiation.output

    result = curl.run_for(
        _ats,
        (
            f"--verbose --http1.1 --tls-max 1.2 --tlsv1.2 --ciphers DEFAULT@SECLEVEL=0 --insecure --resolve "
            f"'example.com:{_ats.https_port}:127.0.0.1' 'https://example.com:{_ats.https_port}/'"),
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.stderr
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    assert "received signal" not in traffic_out and "failed assertion" not in traffic_out
    if "BoringSSL" not in ssl.OPENSSL_VERSION:
        assert "trying to renegotiate from the client" in traffic_out
