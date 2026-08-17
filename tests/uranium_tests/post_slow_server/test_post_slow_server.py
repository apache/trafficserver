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
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class PostSlowServerScenario:
    """Keep an HTTP/2 POST alive across a two-minute origin delay."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Configure the delayed-origin scenario.

        :param ats_factory: Factory that owns the ATS instance.
        :param services: Factory that owns the delayed origin process.
        :param curl: Curl client used for the POST request.
        """

        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        if shutil.which("nc") is None:
            pytest.skip("nc is required")
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._ready_file = ats_factory.run_directory / "origin.ready"
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Create the one-shot server that delays its 200 KB response.

        :param services: Factory that owns the delayed origin process.
        """

        return services.process(
            "origin",
            ("bash", TEST_DIRECTORY / "server.sh", str(self._origin_port), self._ready_file),
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow both sides of the transaction to remain inactive for 150 seconds.

        :param ats_factory: Factory that owns the ATS instance.
        """

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
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
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.proxy_name": "Poxy_Proxy",
                "proxy.config.http.transaction_no_activity_timeout_out": 150,
                "proxy.config.http2.no_activity_timeout_in": 150,
            })
        ats.remap_config.add_line(f"map https://localhost http://127.0.0.1:{self._origin_port}")
        return ats

    def wait_for_origin(self) -> None:
        """Wait until the server script is about to enter its listener."""

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not self._ready_file.exists():
            time.sleep(0.05)
        assert self._ready_file.exists(), self._origin.output
        time.sleep(0.1)

    def run(self) -> None:
        """Send the POST and require the complete delayed response body."""

        self._origin.start()
        self.wait_for_origin()
        self._ats.start()
        output = self._ats.run_directory.parent / "curl.log"
        result = self._curl.run_for(
            self._ats,
            (
                f"--request POST --verbose --ipv4 --http2 --insecure --header 'Content-Length: 0' --output "
                f"'{str(output)}' 'https://localhost:{self._ats.https_port}/xyz'"),
            timeout=150,
        )
        assert result.returncode == 0, result.output
        assert output.stat().st_size == 200 * 1024


@pytest.mark.manual(reason="takes about two minutes")
def test_post_slow_server(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A two-minute origin delay does not terminate an HTTP/2 POST.

    :param ats_factory: Factory that owns the ATS instance.
    :param services: Factory that owns the delayed origin process.
    :param curl: Curl client used for the POST request.
    """

    PostSlowServerScenario(ats_factory, services, curl).run()
