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

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory


class RateLimitSniExpiryScenario:
    """Expire a queued TLS connection while another request holds the active slot."""

    _hostname = "queue-expiry.example.com"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Delay the active request long enough for the queued request to expire."""

        origin = services.origin("origin", delay=4)
        for path, body in (("slow", "SLOW"), ("test", "OK")):
            origin.add_response(
                {
                    "headers": f"GET /{path} HTTP/1.1\r\nHost: queue-expiry.example.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": f"HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n",
                    "body": body,
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure a one-slot SNI limiter with a one-second queue age."""

        ats = ats_factory.create("ts", enable_tls=True)
        if not ats.plugin_exists("rate_limit.so"):
            pytest.skip("rate_limit.so is required")
        ats.write_config_file(
            "rate_limit.yaml",
            "selector:\n"
            f"  - sni: {self._hostname}\n"
            "    limit: 1\n"
            "    queue:\n"
            "      size: 5\n"
            "      max_age: 1\n",
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

    def run(self) -> None:
        """Expire a queued request, then require a healthy follow-up request."""

        self._origin.start()
        self._ats.start()
        url = f"https://{self._hostname}:{self._ats.https_port}"
        resolve = f"--resolve {self._hostname}:{self._ats.https_port}:127.0.0.1"
        result = self._ats.run_shell(
            f"curl -sk --max-time 10 -o /dev/null {url}/slow {resolve} & "
            "sleep 0.5; "
            f"curl -sk --max-time 8 -o /dev/null {url}/queued {resolve} 2>/dev/null || true; "
            "wait; sleep 0.5; "
            f"curl -sk --max-time 10 -o /dev/null -w '%{{http_code}}' {url}/test {resolve}",
            timeout=30,
        )
        assert result.returncode == 0, result.output
        assert "200" in result.stdout
        diags = self._ats.diags_log.read_text(errors="replace")
        assert "FATAL" not in diags
        assert "ink_release_assert" not in diags


def test_rate_limit_sni(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """SNI queue expiry does not underflow the limiter's active-slot count."""

    RateLimitSniExpiryScenario(ats_factory, services).run()
