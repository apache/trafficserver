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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the HTTPS origin used after client authentication.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure strict client verification and load the hook plugin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "server.pem",
        TEST_DIRECTORY / "ssl" / "server.key",
        TEST_DIRECTORY / "ssl" / "signer.pem",
    )
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
            "proxy.config.diags.debug.tags": "ssl_client_verify_test",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.url_remap.pristine_host_hdr": 1,
        })
    for hostname in ("foo.com", "bar.com", "random.com"):
        ats.remap_config.add_line(f"map https://{hostname}:{ats.https_port}/ https://127.0.0.1:{_origin.https_port}")
    ats.write_config_file(
        "sni.yaml",
        "sni:\n- fqdn: bar.com\n  verify_client: STRICT\n- fqdn: foo.com\n  verify_client: STRICT\n",
    )
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_client_verify_test.so")
    ats.plugin_config.add_line("ssl_client_verify_test.so -count=2 -good=foo.com")
    return ats


def request(certificate: str, key: str, expected_code: int, *, _ats: ATS, _curl: Curl) -> str:
    """Send one TLS 1.2 request with the selected client certificate.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param certificate: Certificate used by this test step.
    :param key: Key used by this test step.
    :param expected_code: Expected code for this case.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--tls-max 1.2 --insecure --cert '{str(TEST_DIRECTORY / 'ssl' / certificate)}' --key "
            f"'{str(TEST_DIRECTORY / 'ssl' / key)}' --resolve 'foo.com:{_ats.https_port}:127.0.0.1' "
            f"'https://foo.com:{_ats.https_port}/case1'"),
    )
    assert result.returncode == expected_code, result.output
    return result.output


def test_tls_hooks_client_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """CLIENT_VERIFY_HOOK consistently handles valid and invalid certificates.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    version = re.search(r"\d+(?:\.\d+)+", ssl.OPENSSL_VERSION)
    if version is None or tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    if curl.uses_uds:
        pytest.skip("client certificate verification requires a TCP TLS listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    assert "Could Not Connect" not in request("signed-foo.pem", "signed-foo.key", 0, _ats=_ats, _curl=curl)
    assert "error" in request("signed-bar.pem", "signed-bar.key", 35, _ats=_ats, _curl=curl).lower()
    assert "error" in request("server.pem", "server.key", 35, _ats=_ats, _curl=curl).lower()
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    for outcome in ("good HS", "error HS"):
        for callback in (0, 1):
            expression = rf"Client verify callback {callback} [\da-fx]+? - event is good {outcome}"
            assert re.search(expression, traffic_out), traffic_out
