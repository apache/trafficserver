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
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class NghttpScenario:
    """Exercise HTTP/2 trailers and graceful shutdown with nghttp."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._httpbin = self.configure_httpbin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_httpbin(services: ServiceFactory) -> HttpBinServer:
        """Start the HTTP behavior origin used by both requests."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Terminate h2 and trigger graceful shutdown on the drip request."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        rule = TEST_DIRECTORY / "rules" / "graceful_shutdown.conf"
        ats.copy_to_config(rule)
        ats.remap_config.add_line(
            f"map /httpbin/ http://127.0.0.1:{self._httpbin.port}/ "
            f"@plugin=header_rewrite.so @pparam={ats.config_directory / rule.name}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2_cs",
        })
        return ats

    def run(self) -> None:
        """Send a trailer-bearing POST, then observe both GOAWAY frames."""

        if shutil.which("nghttp") is None:
            pytest.skip("nghttp is required")
        post_body = self._ats.run_directory / "post_body"
        post_body.parent.mkdir(parents=True, exist_ok=True)
        post_body.write_text("0123456789abcdef" * 8192)
        self._httpbin.start()
        self._ats.start()

        trailer = self._ats.run(
            "nghttp",
            "-vn",
            "--no-dep",
            f"https://127.0.0.1:{self._ats.https_port}/httpbin/post",
            "--trailer",
            "foo: bar",
            "-d",
            post_body.name,
            timeout=10,
        )
        assert trailer.returncode == 0, trailer.output
        assert_matches_gold(trailer.stdout, TEST_DIRECTORY / "gold" / "nghttp_0_stdout.gold")

        shutdown = self._ats.run(
            "nghttp",
            "-vn",
            "--no-dep",
            f"https://127.0.0.1:{self._ats.https_port}/httpbin/drip?duration=3",
            timeout=10,
        )
        assert shutdown.returncode == 0, shutdown.output
        assert_matches_gold(shutdown.stdout, TEST_DIRECTORY / "gold" / "nghttp_1_stdout.gold")
        traffic_out = wait_for_file_lines(self._ats.traffic_out, "session free", 2)
        assert_matches_gold(traffic_out, TEST_DIRECTORY / "gold" / "nghttp_ts_stderr.gold")


def test_nghttp(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS forwards h2 trailers and performs a two-stage graceful shutdown."""

    NghttpScenario(ats_factory, services).run()
