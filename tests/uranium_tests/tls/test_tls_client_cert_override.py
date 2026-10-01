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
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


def tls_client_cert_override_configure_origin(
    services: ServiceFactory,
    name: str,
    client_ca: str,
    certificate: str,
    key: str,
) -> OriginServer:
    """Create an HTTPS origin that requires a client certificate.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param client_ca: Client ca used by this test step.
    :param certificate: Certificate used by this test step.
    :param key: Key used by this test step.
    """

    origin = services.origin(
        name,
        ssl=True,
        clientcert=SSL_DIRECTORY / certificate,
        clientkey=SSL_DIRECTORY / key,
        options={
            "--clientCA": SSL_DIRECTORY / client_ca,
            "--clientverify": "",
        },
    )
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def tls_client_cert_override_remap_with_certificate(ats: ATS, path: str, origin: OriginServer, certificate: str, key: str) -> str:
    """Build one client-certificate remap rule.

    :param ats: Traffic Server instance configured or queried by this step.
    :param path: Resource or file path used by this operation.
    :param origin: Configured origin service.
    :param certificate: Certificate used by this test step.
    :param key: Key used by this test step.
    """

    return (
        f"map {path} https://127.0.0.1:{origin.https_port}/ "
        "@plugin=conf_remap.so "
        f"@pparam=proxy.config.ssl.client.cert.filename={certificate} "
        f"@pparam=proxy.config.ssl.client.private_key.filename={key}")


def tls_client_cert_override_configure_ats(
        ats_factory: ATSFactory, *, _server1: OriginServer, _server2: OriginServer, _use_secret_plugin: bool) -> ATS:
    """Configure matching and mismatched client-certificate routes.

    :param _server1: Test-local server1 configured by the test.
    :param _server2: Test-local server2 configured by the test.
    :param _use_secret_plugin: Test-local use secret plugin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("conf_remap.so"):
        pytest.skip("conf_remap.so is not installed")
    ats.copy_to_ssl(
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "signed-foo.pem",
        SSL_DIRECTORY / "signed-foo.key",
        SSL_DIRECTORY / "signed2-foo.pem",
        SSL_DIRECTORY / "signed-bar.pem",
        SSL_DIRECTORY / "signed2-bar.pem",
        SSL_DIRECTORY / "signed-bar.key",
    )
    client_directory = ats.ssl_directory.parent if _use_secret_plugin else ats.ssl_directory
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl",
            "proxy.config.ssl.client.cert.path": str(client_directory),
            "proxy.config.ssl.client.cert.filename": "signed-foo.pem",
            "proxy.config.ssl.client.private_key.path": str(client_directory),
            "proxy.config.ssl.client.private_key.filename": "signed-foo.key",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    if _use_secret_plugin:
        ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
        ats.plugin_config.add_line("ssl_secret_load_test.so")
        ats.write_config_file("sni.yaml", "sni:\n  - fqdn: random\n    verify_server_properties: NONE\n")
    ats.remap_config.add_lines(
        (
            tls_client_cert_override_remap_with_certificate(ats, "/case1", _server1, "signed-foo.pem", "signed-foo.key"),
            tls_client_cert_override_remap_with_certificate(ats, "/badcase1", _server1, "signed2-foo.pem", "signed-foo.key"),
            tls_client_cert_override_remap_with_certificate(ats, "/case2", _server2, "signed2-foo.pem", "signed-foo.key"),
            tls_client_cert_override_remap_with_certificate(ats, "/badcase2", _server2, "signed-foo.pem", "signed-foo.key"),
        ))
    return ats


def tls_client_cert_override_request(path: str, host: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request one client-certificate selection case.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    :param host: HTTP host name used for the request.
    """

    result = _curl.get(_ats, path, headers={"Host": host})
    assert result.returncode == 0, result.output
    return result.stdout


def tls_client_cert_override_verify_secret_updates(*, _ats: ATS, _curl: Curl) -> None:
    """Verify reload-triggered and polled in-place client-certificate changes.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    shutil.copy2(SSL_DIRECTORY / "signed-foo.pem", _ats.ssl_directory / "signed2-foo.pem")
    (_ats.config_directory / "sni.yaml").touch()
    result = _ats.traffic_ctl(
        "config",
        "set",
        "proxy.config.ssl.client.cert.path",
        str(_ats.ssl_directory.parent),
    )
    assert result.returncode == 0, result.output
    result = _ats.traffic_ctl("config", "reload", "-m", "-T", "30s")
    assert result.returncode == 0, result.output
    assert "Could Not Connect" not in tls_client_cert_override_request("/badcase1", "foo.com", _ats=_ats, _curl=_curl)

    shutil.copy2(SSL_DIRECTORY / "signed2-foo.pem", _ats.ssl_directory / "signed-foo.pem")
    (_ats.ssl_directory / "signed-foo.pem").touch()
    (_ats.ssl_directory / "signed-foo.key").touch()
    time.sleep(4)
    assert "Could Not Connect" in tls_client_cert_override_request("/case1", "example.com", _ats=_ats, _curl=_curl)
    assert "Could Not Connect" not in tls_client_cert_override_request("/badcase1", "example.com", _ats=_ats, _curl=_curl)


def run_tls_client_cert_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_secret_plugin: bool) -> None:
    """conf_remap selects an outbound client certificate per mapping.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_secret_plugin: Whether to load client certificates through the secret plugin.
    """
    _server1 = tls_client_cert_override_configure_origin(
        services,
        "server",
        "signer.pem",
        "signed-foo.pem",
        "signed-foo.key",
    )
    _server2 = tls_client_cert_override_configure_origin(
        services,
        "server2",
        "signer2.pem",
        "signed2-bar.pem",
        "signed-bar.key",
    )
    _ats = tls_client_cert_override_configure_ats(
        ats_factory, _server1=_server1, _server2=_server2, _use_secret_plugin=use_secret_plugin)

    _server1.start()
    _server2.start()
    _ats.start()
    assert "Could Not Connect" not in tls_client_cert_override_request("/case1", "example.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" in tls_client_cert_override_request("/badcase1", "example.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" not in tls_client_cert_override_request("/case2", "bar.com", _ats=_ats, _curl=curl)
    assert "Could Not Connect" in tls_client_cert_override_request("/badcase2", "bar.com", _ats=_ats, _curl=curl)
    if use_secret_plugin:
        tls_client_cert_override_verify_secret_updates(_ats=_ats, _curl=curl)


def test_tls_client_cert_override(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """conf_remap selects an outbound client certificate per mapping.

    :param ats_factory: Factory for isolated ATS instances.
    :param services: Factory owning the TLS origins.
    :param curl: Transport-aware client.
    """
    run_tls_client_cert_override(ats_factory, services, curl, use_secret_plugin=False)
