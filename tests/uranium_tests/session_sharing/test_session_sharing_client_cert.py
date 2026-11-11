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
"""Pooled TLS origin connections must not cross client-certificate identities."""

from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory, Curl


@pytest.mark.parametrize("case", ("different-cert", "same-cert", "no-cert"))
def test_session_sharing_client_cert(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, case: str) -> None:
    """Check certificate rejection with positive same-identity and no-cert controls.

    :param ats_factory: Proxy factory.
    :param services: TLS microserver factory.
    :param curl: Transport-aware request client.
    :param case: Different, same, or absent outbound client certificate.
    """
    ssl = Path(__file__).parents[1] / "tls/ssl"
    require_cert = case != "no-cert"
    options = {"--clientCA": str(ssl / "signer.pem"), "--clientverify": ""} if require_cert else {}
    origin = services.origin("origin", ssl=True, options=options)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nContent-Length: 0\r\n\r\n"},
    )
    ats = ats_factory.create("ts")
    if not ats.plugin_exists("conf_remap.so"):
        pytest.skip("conf_remap.so is required")
    ats.copy_to_ssl(ssl / "signer.pem", ssl / "signed-foo.pem", ssl / "signed-san.pem")
    for path, cert in (("one", "signed-foo.pem"), ("two", "signed-san.pem")):
        rule = f"map /{path} https://127.0.0.1:{origin.https_port}/"
        if require_cert:
            rule += f" @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename={cert}"
        ats.remap_config.add_line(rule)
    # Leave server_session_sharing.match unset: the shipped default is under test.
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_ss",
            "proxy.config.http.server_session_sharing.pool": "global",
            "proxy.config.ssl.client.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.verify.server.policy": "DISABLED",
        }
    )
    origin.start()
    ats.start()
    for path in ("one", "one" if case == "same-cert" else "two"):
        result = curl.run_for(ats, f"-vs -H 'Host: www.example.com' -H 'Connection: close' http://127.0.0.1:{ats.http_port}/{path}")
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200" in result.stderr, result.output
    ats.stop()
    log = ats.traffic_out.read_text()
    assert ("global pool search successful" in log) == (case != "different-cert"), log
    if case == "different-cert":
        assert "validate_cert proposed_cert=signed-san.pem, cert=signed-foo.pem" in log, log
