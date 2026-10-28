#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements. See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership. The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License. You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import re

from tools.uranium.services import ATSFactory, Curl, ServiceFactory


def test_tls_hooks_verify_tls12(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A permissive verify-hook failure completes one TLS 1.2 origin handshake.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the TLS origin.
    :param curl: Transport-aware curl runner.
    """
    origin = services.origin("server", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_verify_test",
            "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
            "proxy.config.ssl.client.verify.server.properties": "NONE",
            "proxy.config.ssl.client.TLSv1_3.enabled": 0,
            "proxy.config.ssl.origin_session_cache.enabled": 0,
            "proxy.config.url_remap.pristine_host_hdr": 1,
        })
    ats.remap_config.add_line(f"map https://bar.com:{ats.https_port}/ https://127.0.0.1:{origin.https_port}")
    ats.write_config_file("sni.yaml", "sni:\n- fqdn: bar.com\n  verify_server_policy: PERMISSIVE\n")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_verify_test.so")
    ats.plugin_config.add_line("ssl_verify_test.so -count=1 -bad=bar.com")
    origin.start()
    ats.start()
    result = curl.run_for(
        ats, f"--resolve bar.com:{ats.https_port}:127.0.0.1 -k -s -o /dev/null "
        f"-w 'status=%{{http_code}}' https://bar.com:{ats.https_port}")
    assert result.returncode == 0, result.output
    assert "status=200" in result.stdout
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
    assert (
        "WARNING: TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for 127.0.0.1.  "
        "Action=Continue SNI=bar.com") in ats.diags_log.read_text(errors="replace")
    output = ats.traffic_out.read_text(errors="replace")
    assert re.search(r"Server verify callback 0 [\da-fx]+? - event is good SNI=bar.com error HS", output), output
    assert len(re.findall(r"Server verify callback 0 [^\n]*SNI=bar.com", output)) == 1, output
