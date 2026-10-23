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


def test_tls_sni_yaml_reload(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An invalid sni.yaml reload rolls back without changing active policy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    __hostname = "example.com"

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the reusable empty-response origin.

        :param services: Factory owning support services and their cleanup.
        """

        origin = services.origin("server")
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: {__hostname}\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def sni_document(*, valid: bool) -> str:
        """Render the valid initial or intentionally invalid replacement policy.

        :param valid: Valid used by this test step.
        """

        suffix = "foo" if valid else "notexist"
        http2 = "off" if valid else "on"
        return (
            "sni:\n"
            f"- fqdn: {__hostname}\n"
            f"  http2: {http2}\n"
            f"  client_cert: {_ats.ssl_directory}/signed-{suffix}.pem\n"
            f"  client_key: {_ats.ssl_directory}/signed-{suffix}.key\n"
            "  verify_client: STRICT\n")

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure the initial valid SNI policy and trust store.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """
        nonlocal _ats

        ats = ats_factory.create("ts", enable_tls=True, disable_log_checks=True)
        _ats = ats
        ats.add_default_ssl_files()
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "signed-foo.pem",
            TEST_DIRECTORY / "ssl" / "signed-foo.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.records.update(
            {
                "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl|http",
                "proxy.config.diags.output.debug": "L",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.write_config_file("sni.yaml", sni_document(valid=True))
        return ats

    def request(certificate: str, key: str) -> str:
        """Send one TLS 1.2 request with a trusted client certificate.

        :param certificate: Certificate used by this test step.
        :param key: Key used by this test step.
        """

        result = curl.run_for(
            _ats,
            (
                f"--tls-max 1.2 --silent --verbose --insecure --cert '{str(TEST_DIRECTORY / 'ssl' / certificate)}' "
                f"--key '{str(TEST_DIRECTORY / 'ssl' / key)}' --resolve "
                f"'{__hostname}:{_ats.https_port}:127.0.0.1' "
                f"'https://{__hostname}:{_ats.https_port}'"),
        )
        assert result.returncode == 0, result.output
        return result.output

    if curl.uses_uds:
        pytest.skip("SNI client-certificate coverage requires a TCP listener")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)

    _origin.start()
    _ats.start()
    initial = request("signed-foo.pem", "signed-foo.key")
    assert "Could Not Connect" not in initial
    assert __hostname in initial

    (_ats.config_directory / "sni.yaml").write_text(sni_document(valid=False))
    reload_result = _ats.traffic_ctl("config", "reload", "-m", "-t", "invalid-sni-reload", "-w", "1", "-r", "0.5", "-T", "30s")
    assert reload_result.returncode == 2, reload_result.output

    final = request("signed-bar.pem", "signed-bar.key")
    assert "GET / HTTP/2" not in final
