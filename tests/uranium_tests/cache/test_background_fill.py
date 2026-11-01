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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, assert_matches_gold

GOLD_DIRECTORY = Path(__file__).parent / "gold"


def test_background_fill(ats_factory: ATSFactory, curl: Curl, services: ServiceFactory) -> None:
    """Exercise background fill over HTTP/1.1, TLS, and HTTP/2.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    :param services: Factory owning support services and their cleanup.
    """

    def _configure_ats(ats: ATS, origin_port: int) -> None:
        """configure ats.

        :param ats: Traffic Server instance configured or queried by this step.
        :param origin_port: Allocated origin listener port number.
        """
        ats.add_default_ssl_files()
        ats.ssl_multicert_config.add_lines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""")
        ats.records.update(
            {
                "proxy.config.http.server_ports": f"{ats.http_port} {ats.https_port}:ssl {ats.uds_path}",
                "proxy.config.http.background_fill_active_timeout": "0",
                "proxy.config.http.background_fill_completed_threshold": "0.0",
                "proxy.config.http.cache.required_headers": 0,
                "proxy.config.http.insert_response_via_str": 2,
                "proxy.config.http.server_session_sharing.pool": "thread",
                "proxy.config.http.server_session_sharing.match": "ip,sni,cert",
                "proxy.config.exec_thread.autoconfig.enabled": 0,
                "proxy.config.exec_thread.limit": 1,
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.diags.debug.enabled": 3,
                "proxy.config.diags.debug.tags": "http",
            })
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{origin_port}")

    if not curl.supports("http2"):
        pytest.skip("curl lacks HTTP/2 support")
    httpbin = services.httpbin("httpbin")
    for_httpbin = ats_factory.create("for_httpbin", enable_tls=True, enable_cache=True)
    _configure_ats(for_httpbin, httpbin.port)
    httpbin.start()
    for_httpbin.start()
    scripts = [
        (
            f"""
{{curl}} -X PURGE --http1.1 -vs http://127.0.0.1:{for_httpbin.http_port}/drip?duration=4
timeout 1 {{curl}} --http1.1 -vs http://127.0.0.1:{for_httpbin.http_port}/drip?duration=4 || true
sleep 5
{{curl}} --http1.1 -vs http://127.0.0.1:{for_httpbin.http_port}/drip?duration=4 -H "x-debug: x-cache"
""",
            "background_fill_0_stderr_H.gold",
        ),
    ]
    if not curl.uses_uds:
        scripts.extend(
            [
                (
                    f"""
{{curl}} -X PURGE --http1.1 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4
timeout 1 {{curl}} --http1.1 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4 || true
sleep 5
{{curl}} --http1.1 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4 -H "x-debug: x-cache"
""",
                    "background_fill_1_stderr_H.gold",
                ),
                (
                    f"""
{{curl}} -X PURGE --http2 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4
timeout 1 {{curl}} --http2 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4 || true
sleep 5
{{curl}} --http2 -vsk https://127.0.0.1:{for_httpbin.https_port}/drip?duration=4 -H "x-debug: x-cache"
""",
                    "background_fill_2_stderr_H.gold",
                ),
            ])

    for script, gold_name in scripts:
        result = curl.run_script(for_httpbin, script, timeout=20)
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stderr, GOLD_DIRECTORY / gold_name)
