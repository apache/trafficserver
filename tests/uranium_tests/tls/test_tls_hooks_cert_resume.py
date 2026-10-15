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
"""Resuming a paused TLS hook must continue at the correct hook stage."""
import re
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory, Curl


@pytest.mark.parametrize("mode", ("verify", "switch", "chain"))
def test_tls_hooks_cert_resume(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, mode: str) -> None:
    """Retain hook events, selected certificates, and SNI trust across pauses.

    :param ats_factory: Proxy factory.
    :param services: Origin factory.
    :param curl: TLS client.
    :param mode: Verify-client, certificate-switch, or complete hook-chain case.
    """
    ssl = Path(__file__).parent / "ssl"
    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
            "body": ""
        }, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
            "body": "ok"
        })
    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(ssl / "server.pem", ssl / "server.key", ssl / "signer.pem")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/ssl_hook_test.so")
    hook_args = {
        "verify": "-cert=1",
        "switch": "-client_hello=1 -cert=1 -cert_switch=bar.com",
        "chain": "-client_hello=1 -sni=1 -cert=1"
    }[mode]
    ats.plugin_config.add_line(f"ssl_hook_test.so {hook_args}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_hook_test|ssl_client_verify_test"
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    multicert = [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]
    if mode == "verify":
        ats.records.update({"proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem")})
        ats.write_config_file("sni.yaml", "sni:\n- fqdn: foo.com\n  verify_client: STRICT\n")
        ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/ssl_client_verify_test.so")
        ats.plugin_config.add_line("ssl_client_verify_test.so -count=1 -good=foo.com")
    elif mode == "switch":
        for name in ("signed-foo", "signed-bar"):
            ats.copy_to_ssl(ssl / f"{name}.pem", ssl / f"{name}.key")
        ats.write_config_file(
            "sni.yaml", f"sni:\n- fqdn: foo.com\n  verify_client: STRICT\n"
            f"  verify_client_ca_certs: {ats.ssl_directory}/signer.pem\n")
        multicert = [
            {
                "dest_ip": "192.0.2.1",
                "ssl_cert_name": "signed-foo.pem",
                "ssl_key_name": "signed-foo.key"
            }, {
                "dest_ip": "*",
                "ssl_cert_name": "signed-bar.pem",
                "ssl_key_name": "signed-bar.key"
            }
        ]
    ats.set_ssl_multicert_yaml({"ssl_multicert": multicert})
    origin.start()
    ats.start()
    cases = [("--tls-max 1.2", False), ("--tlsv1.3", False)]
    if mode == "verify":
        cases.append(("--tls-max 1.2", True))
    for version, authenticated in cases:
        sni = "random.com" if mode == "verify" and not authenticated else "foo.com"
        auth = f"--cert {ssl}/signed-foo.pem --key {ssl}/signed-foo.key" if authenticated or mode == "switch" else ""
        result = curl.run_for(
            ats, f"-v -k --max-time 15 -o /dev/null -w 'status=%{{http_code}}' {version} {auth} "
            f"-H uuid:basic --resolve '{sni}:{ats.https_port}:127.0.0.1' https://{sni}:{ats.https_port}/")
        assert result.returncode == 0, result.output
        assert "status=200" in result.stdout
        if mode == "switch":
            assert "CN=bar.com" in result.output
            assert not re.search("CN=foo.com|CN=random.server.com", result.output)
    ats.stop()
    output = ats.traffic_out.read_text(errors="replace")
    assert "event is bad" not in output
    assert re.search(r"Cert callback 0 .* - event is good", output)
    if mode == "verify":
        assert re.search(r"Client verify callback 0 .* - event is good good HS", output)
        assert output.count("Client verify callback") == 1
    else:
        assert re.search(r"Client Hello callback 0 .* - event is good", output)
        if mode == "switch":
            assert re.search("Switched ssl_vc=.* to the bar.com certificate", output)
        else:
            assert re.search(r"SNI callback 0 .* - event is good", output)
