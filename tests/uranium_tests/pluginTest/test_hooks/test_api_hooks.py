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

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class ApiHooksScenario:
    """Exercise HTTP and TLS hooks with strictly sequential client connections."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._hook_log = self._ats.log_directory / "log.txt"

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Serve the request used by each client protocol."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /argh HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the hook test plugin on clear-text and TLS listeners."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.proxy_name": "Poxy_Proxy",
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http|test_hooks",
            })
        ats.copy_custom_plugin("{AtsTestPluginsDir}/test_hooks.so")
        ats.plugin_config.add_line("test_hooks.so")
        ats.remap_config.add_lines(
            [
                f"map http://one http://127.0.0.1:{self._origin.port}",
                f"map https://one http://127.0.0.1:{self._origin.port}",
            ])
        ats.set_environment("OUTPUT_FILE", str(ats.log_directory / "log.txt"))
        return ats

    @staticmethod
    def verify_response(result: CommandResult) -> None:
        """Require a successful curl response without coupling to verbose formatting."""

        assert result.returncode == 0, result.output
        assert "< HTTP/1.1 200" in result.stderr or "< HTTP/2 200" in result.stderr

    def request_cleartext(self) -> None:
        """Exercise the ordinary HTTP session and transaction hooks."""

        result = self._curl.get(self._ats, "/argh", headers={"Host": "one"}, options=("--verbose",))
        self.verify_response(result)

    def request_tls(self) -> None:
        """Exercise HTTP/2 and HTTP/1.1 TLS hooks on separate connections."""

        if self._curl.uses_uds:
            return
        if not self._curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        for version in ("--http2", "--http1.1"):
            result = self._curl.run(
                "--verbose",
                "--ipv4",
                version,
                "--insecure",
                "--header",
                "Host: one",
                f"https://127.0.0.1:{self._ats.https_port}/argh",
            )
            self.verify_response(result)

    def verify_hooks(self) -> None:
        """Compare the complete ordered callback trace after close hooks run."""

        expected_sessions = 1 if self._curl.uses_uds else 3
        wait_for_file_lines(self._hook_log, r"^Session: event=TS_EVENT_HTTP_SSN_CLOSE$", expected_sessions)
        gold = "log_uds.gold" if self._curl.uses_uds else "log.gold"
        assert_matches_gold(self._hook_log.read_text(errors="replace"), TEST_DIRECTORY / gold)

    def run(self) -> None:
        """Start the services, run each protocol serially, and inspect the callback trace."""

        self._origin.start()
        self._ats.start()
        self.request_cleartext()
        self.request_tls()
        self.verify_hooks()


def test_api_hooks(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Hook callbacks fire in the expected order for HTTP/1.1 and HTTP/2 sessions."""

    ApiHooksScenario(ats_factory, services, curl).run()
