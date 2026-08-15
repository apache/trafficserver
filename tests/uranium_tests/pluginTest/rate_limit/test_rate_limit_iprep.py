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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class RateLimitIpReputationScenario:
    """Exercise SNI IP-reputation buckets through repeated TLS handshakes."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve the request used to increment the reputation buckets."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /test HTTP/1.1\r\nHost: iprep.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
                "body": "OK"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure five IP-reputation buckets and the SNI selector."""

        ats = ats_factory.create("ts", enable_tls=True)
        if not ats.plugin_exists("rate_limit.so"):
            pytest.skip("rate_limit.so is required")
        ats.write_config_file(
            "rate_limit.yaml",
            "ip-rep:\n"
            "  - name: test-iprep\n"
            "    buckets: 5\n"
            "    size: 10\n"
            "    percentage: 90\n"
            "    max_age: 300\n"
            "selector:\n"
            "  - sni: iprep.example.com\n"
            "    limit: 100\n"
            "    ip-rep: test-iprep\n",
        )
        ats.plugin_config.add_line(f"rate_limit.so {ats.config_directory}/rate_limit.yaml")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rate_limit",
                "proxy.config.http.insert_response_via_str": 0,
                "proxy.config.url_remap.remap_required": 0,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    def request(self) -> CommandResult:
        """Open a fresh TLS connection with the selector's SNI."""

        return self._curl.run(
            "--silent",
            "--insecure",
            "--output",
            "/dev/null",
            "--write-out",
            "%{http_code}",
            "--resolve",
            f"iprep.example.com:{self._ats.https_port}:127.0.0.1",
            f"https://iprep.example.com:{self._ats.https_port}/test",
        )

    def run(self) -> None:
        """Start the topology and increment the buckets several times."""

        self._origin.start()
        self._ats.start()
        for _ in range(6):
            result = self.request()
            assert result.returncode == 0, result.output
            assert result.stdout == "200", result.output
        assert "FATAL" not in self._ats.diags_log.read_text(errors="replace")


def test_rate_limit_iprep(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """IP-reputation buckets are sized before indexed initialization and updates."""

    RateLimitIpReputationScenario(ats_factory, services, curl).run()
