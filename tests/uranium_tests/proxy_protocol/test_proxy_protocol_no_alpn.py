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
"""Verify TLS over PROXY protocol succeeds when the client omits ALPN."""

import shlex

from tools.uranium.services import ATSFactory, Curl, ServiceFactory


class TestProxyProtocolTLSWithoutALPN:
    """PROXY protocol TLS must work when the client does not negotiate ALPN."""

    def test_proxy_protocol_tls_without_alpn(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """A TLS client using PROXY protocol and HTTP/1.1 need not negotiate ALPN.

        :param ats_factory: Factory owning the isolated Traffic Server instance.
        :param services: Factory owning the origin server.
        :param curl: Curl runner for the test sandbox.
        """
        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: no-alpn.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "body": "OK",
            },
        )

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port}/")
        ats.records.update(
            {
                "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
                "proxy.config.http.insert_response_via_str": 0,
                "proxy.config.url_remap.remap_required": 0,
            }
        )

        origin.start()
        ats.start()

        port = ats.proxy_protocol_https_port
        arguments = shlex.join(
            [
                "--insecure",
                "--silent",
                "--show-error",
                "--http1.1",
                "--no-alpn",
                "--haproxy-protocol",
                "--output",
                "/dev/null",
                "--write-out",
                "%{http_code}",
                "--resolve",
                f"no-alpn.example.com:{port}:127.0.0.1",
                f"https://no-alpn.example.com:{port}/",
            ]
        )
        result = curl.run(arguments, timeout=15)

        assert result.returncode == 0, result.output
        assert result.stdout == "200", result.output
