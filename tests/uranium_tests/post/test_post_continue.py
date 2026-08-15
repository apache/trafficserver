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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer


class PostContinueScenario:
    """Exercise delayed and immediate 100-continue responses over H1 and H2."""

    _replay = "replay/post-continue.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("the TLS protocol matrix requires TCP listeners")
        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        self._curl = curl
        self._server = self.configure_server(services)
        self._delayed = self.configure_ats(ats_factory, "ts", send_immediately=False)
        self._immediate = self.configure_ats(ats_factory, "ts2", send_immediately=True)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the verifier origin that accepts the repeated POST requests."""

        return services.verifier_server("server", self._replay)

    def configure_ats(self, ats_factory: ATSFactory, name: str, *, send_immediately: bool) -> ATS:
        """Configure one ATS instance's 100-continue response policy."""

        ats = ats_factory.create(name, enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.send_100_continue_response": int(send_immediately),
            })
        return ats

    def run_case(self, ats: ATS, protocol: str, body: str | Path, *, expect_continue: bool) -> None:
        """Run and verify one member of the protocol, size, and policy matrix."""

        expect_header = "Expect: 100-continue" if expect_continue else "Expect:"
        result = self._curl.run_for(
            ats,
            "--verbose",
            "--output",
            "/dev/null",
            f"--{protocol}",
            "--header",
            "uuid: post",
            "--header",
            expect_header,
            "--data",
            f"@{body}" if isinstance(body, Path) else body,
            "--insecure",
            f"https://127.0.0.1:{ats.https_port}/post",
            timeout=30,
        )
        assert result.returncode == 0, result.output
        if protocol == "http2":
            assert "POST /post HTTP/2" in result.output
            assert "HTTP/2 200" in result.output
            continue_response = "HTTP/2 100"
        else:
            assert "> POST /post HTTP/1.1" in result.output
            assert "< HTTP/1.1 200 OK" in result.output
            continue_response = "HTTP/1.1 100"
        if expect_continue:
            assert "xpect: 100-continue" in result.output
            assert continue_response in result.output
        else:
            assert "xpect: 100-continue" not in result.output
            assert continue_response not in result.output

    def run(self) -> None:
        """Run all sixteen combinations against the two ATS policies."""

        self._server.start()
        self._delayed.start()
        self._immediate.start()
        large_body = self._delayed.run_directory.parent / "big_post_body"
        large_body.write_text("0123456789" * 131070)
        for ats in (self._delayed, self._immediate):
            for protocol in ("http1.1", "http2"):
                for body in ("small body", large_body):
                    for expect_continue in (True, False):
                        self.run_case(ats, protocol, body, expect_continue=expect_continue)


def test_post_continue(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS handles Expect: 100-continue across body sizes and HTTP versions."""

    PostContinueScenario(ats_factory, services, curl).run()
