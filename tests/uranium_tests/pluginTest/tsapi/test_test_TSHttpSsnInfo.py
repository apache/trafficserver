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
import re
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, HttpBinServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class HttpSessionInfoScenario:
    """Drive HTTP/2 session APIs and frame counters from a test plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._enable_quic = ats_factory.has_feature("TS_USE_QUIC") and curl.supports("http3")
        self._httpbin = self.configure_httpbin(services)
        self._ats = self.configure_ats(ats_factory)
        self._log = self._ats.log_directory / "test_TSHttpSsnInfo_plugin_log.txt"

    @staticmethod
    def configure_httpbin(services: ServiceFactory) -> HttpBinServer:
        """Create the POST origin used by the protocol clients."""

        return services.httpbin("httpbin")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the session-info plugin and enable TLS plus optional QUIC."""

        ats = ats_factory.create("ts", enable_tls=True, enable_quic=self._enable_quic)
        ats.add_default_ssl_files()
        ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/pluginTest/tsapi/.libs/test_TSHttpSsnInfo.so")
        ats.plugin_config.add_line("test_TSHttpSsnInfo.so")
        ats.set_environment("OUTPUT_FILE", str(ats.log_directory / "test_TSHttpSsnInfo_plugin_log.txt"))
        ats.remap_config.add_line(f"map /httpbin/ http://127.0.0.1:{self._httpbin.port}/")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2|http3|quic|test_TSHttpSsnInfo",
            })
        return ats

    def run(self) -> None:
        """Send continuation-heavy h2 traffic and inspect plugin counters."""

        if shutil.which("nghttp") is None:
            pytest.skip("nghttp is required")
        post_body = self._ats.run_directory / "post_body"
        post_body.parent.mkdir(parents=True, exist_ok=True)
        post_body.write_text("0123456789abcdef" * 8)
        self._httpbin.start()
        self._ats.start()

        h2 = self._ats.run_shell(
            f"nghttp -vn --continuation 'https://localhost:{self._ats.https_port}/httpbin/post' "
            f"-d '{post_body.name}' | grep -v 'continuation-test'",
            timeout=10,
        )
        assert h2.returncode == 0, h2.output
        assert_matches_gold(h2.stdout, TEST_DIRECTORY / "test_TSHttpSsnInfo_nghttp0.gold")

        if self._enable_quic:
            h3 = self._curl.run_for(
                self._ats,
                f"--insecure --http3 --data post_body 'https://localhost:{self._ats.https_port}/httpbin/post'",
            )
            assert h3.returncode == 0, h3.output
            assert_matches_gold(h3.stdout, TEST_DIRECTORY / "test_TSHttpSsnInfo_curl0.gold")

        log = wait_for_file_lines(self._log, "H2 Frames Received:", 1)
        expected_frames = r"H2 Frames Received:D1,H1,PR\d+,RS0,S2,PP0,P0,G1,WU0,C1,U0"
        assert re.search(expected_frames, log), log
        assert "H2 OOB(11)=0,OOB(1000)=0" in log
        assert_matches_gold(log, TEST_DIRECTORY / "test_TSHttpSsnInfo_plugin_log.gold")


def test_test_TSHttpSsnInfo(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP session APIs report the expected HTTP/2 frame metrics."""

    HttpSessionInfoScenario(ats_factory, services, curl).run()
