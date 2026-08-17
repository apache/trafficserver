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
"""Verify curl-specific ip_allow method filtering and access logging."""

import re
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines


class IpAllowCurlScenario:
    """Exercise GET, CONNECT, and HTTP/2 PUSH through ip_allow."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._origin = self.configure_server()
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    def configure_server(self) -> OriginServer:
        """Create a TLS origin for the allowed GET and denied method canaries."""

        origin = self._services.origin("origin", ssl=True)
        for method, path, version in (("GET", "/get", "1.1"), ("CONNECT", "/connect", "1.1"), ("PUSH", "/h2_push", "2")):
            origin.add_response(
                {"headers": f"{method} {path} HTTP/{version}\r\nHost: www.example.com:80\r\n\r\n"},
                {
                    "headers": f"HTTP/{version} 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n",
                    "body": "xxx",
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow GET, HEAD, and POST while logging denied method details."""

        ats = ats_factory.create("ats", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ip_allow|http|url_rewrite",
                "proxy.config.http.push_method_enabled": 1,
                "proxy.config.http.connect_ports": str(self._origin.https_port),
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.http2.active_timeout_in": 3,
                "proxy.config.http2.max_concurrent_streams_in": 65535,
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._origin.https_port}")
        ats.ip_allow_config.add_lines(
            """ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods: [GET, HEAD, POST]
  - apply: in
    ip_addrs: ::/0
    action: allow
    methods: [GET, HEAD, POST]
""")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "custom",
                                    "format":
                                        (
                                            "scheme=%<pqus> %<cqtd>-%<cqtt> %<stms> %<ttms> %<chi> %<crc>/%<pssc> %<psql> "
                                            "%<cqhm> %<pquc> %<phr> %<psct> %<{Y-RID}pqh> %<{Y-YPCS}pqh> %<{Host}cqh> "
                                            "%<{CHAD}pqh>  sftover=%<{x-safet-overlimit-rules}cqh> "
                                            "sftmat=%<{x-safet-matched-rules}cqh> sftcls=%<{x-safet-classification}cqh> "
                                            "sftbadclf=%<{x-safet-bad-classifiers}cqh> yra=%<{Y-RA}cqh> status_setter=%<prscs>"),
                                }
                            ],
                        "logs": [{
                            "filename": "squid.log",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def request(self, *arguments: str, status: str) -> None:
        """Run curl and verify the returned HTTP status."""

        result = self._curl.run_for(
            self._ats,
            f"--verbose {shlex.join(arguments)}",
        )
        assert result.returncode == 0, result.output
        assert status in result.stderr, result.output

    def validate_access_log(self) -> None:
        """Normalize timing and ports before comparing the access log."""

        log = self._ats.log_directory / "squid.log"
        content = wait_for_file_lines(log, "status_setter=", 3, timeout=15)
        content = re.sub(r"^(\S+) \S+ \S+ \S+ ", r"\1 ", content, flags=re.MULTILINE)
        content = re.sub(r":[0-9]+([^0-9])", r":SOMEPORT\1", content)
        assert_matches_gold(content, self._services.resolve_path("gold/log.gold"))

    def run(self) -> None:
        """Run the three methods and validate diagnostics and access output."""

        self._origin.start()
        self._ats.start()
        self.request("--header", "Host: www.example.com", f"http://127.0.0.1:{self._ats.http_port}/get", status="200 OK")
        self.request(
            "--request",
            "CONNECT",
            "--header",
            "Host: localhost",
            f"http://127.0.0.1:{self._ats.http_port}/connect",
            status="HTTP/1.1 403",
        )
        self.request(
            "--http2",
            "--insecure",
            "--request",
            "PUSH",
            "--header",
            "Host: localhost",
            f"https://127.0.0.1:{self._ats.https_port}/h2_push",
            status="HTTP/2 403",
        )
        wait_for_file_lines(self._ats.traffic_out, "Line 1 denial for 'CONNECT' from 127.0.0.1", 1)
        wait_for_file_lines(self._ats.traffic_out, "Line 1 denial for 'PUSH' from 127.0.0.1", 1)
        self.validate_access_log()


def test_ip_allow(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ip_allow rejects disallowed curl methods before contacting the origin."""

    if Curl(ats_factory.run_directory).uses_uds:
        pytest.skip("ip_allow client-address checks require IP listeners")
    IpAllowCurlScenario(ats_factory, services).run()
