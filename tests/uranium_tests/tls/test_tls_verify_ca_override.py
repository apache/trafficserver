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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


def configure_origin(services: ServiceFactory, name: str, certificate: str) -> OriginServer:
    """Create an HTTPS origin signed by one of the two test CAs.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param certificate: Certificate used by this test step.
    """

    origin = services.origin(
        name,
        ssl=True,
        clientkey=SSL_DIRECTORY / "signed-foo.key",
        clientcert=SSL_DIRECTORY / certificate,
    )
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def ca_overrides(ats: ATS, filename: str) -> str:
    """Build the conf_remap CA record overrides.

    :param ats: Traffic Server instance configured or queried by this step.
    :param filename: Filename used by this test step.
    """

    return (
        f"@pparam=proxy.config.ssl.client.CA.cert.path={ats.ssl_directory} "
        f"@pparam=proxy.config.ssl.client.CA.cert.filename={filename}")


def configure_ats(ats_factory: ATSFactory, *, _server1: OriginServer, _server2: OriginServer) -> ATS:
    """Map success and failure paths to each origin CA.

    :param _server1: Test-local server1 configured by the test.
    :param _server2: Test-local server2 configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("conf_remap.so"):
        pytest.skip("conf_remap.so is not installed")
    ats.copy_to_ssl(
        SSL_DIRECTORY / "signed-foo.pem",
        SSL_DIRECTORY / "signed2-foo.pem",
        SSL_DIRECTORY / "signed-foo.key",
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "signer.pem",
        SSL_DIRECTORY / "signer.key",
        SSL_DIRECTORY / "signer2.pem",
        SSL_DIRECTORY / "signer2.key",
    )
    ats.remap_config.add_lines(
        (
            f"map /case1 https://127.0.0.1:{_server1.https_port}/ "
            f"@plugin=conf_remap.so {ca_overrides(ats, 'signer.pem')}",
            f"map /badcase1 https://127.0.0.1:{_server1.https_port}/ "
            f"@plugin=conf_remap.so {ca_overrides(ats, 'signer2.pem')}",
            f"map /case2 https://127.0.0.1:{_server2.https_port}/ "
            f"@plugin=conf_remap.so {ca_overrides(ats, 'signer2.pem')}",
            f"map /badcase2 https://127.0.0.1:{_server2.https_port}/ "
            f"@plugin=conf_remap.so {ca_overrides(ats, 'signer.pem')}",
        ))
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
            "proxy.config.ssl.client.verify.server.properties": "SIGNATURE",
            "proxy.config.ssl.client.CA.cert.path": "/tmp",
            "proxy.config.ssl.client.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.url_remap.pristine_host_hdr": 1,
        })
    return ats


def request(path: str, host: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request one CA override case and return the response body.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param host: HTTP host name used for the request.
    """

    result = _curl.get(_ats, path, headers={"Host": host})
    assert result.returncode == 0, result.output
    return result.stdout


def test_tls_verify_ca_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """conf_remap selects the correct CA bundle for each outbound TLS origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _server1 = configure_origin(services, "server1", "signed-foo.pem")
    _server2 = configure_origin(services, "server2", "signed2-foo.pem")
    _ats = configure_ats(ats_factory, _server1=_server1, _server2=_server2)

    _server1.start()
    _server2.start()
    _ats.start()
    first = request("/case1", "foo.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" not in first, _ats.diags_log.read_text(errors="replace")
    assert "Could Not Connect" in request("/badcase1", "bar.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" not in request("/case2", "random.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" in request("/badcase2", "foo.com", _ats=_ats, _curl=curl)
