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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class PostEarlyReturnScenario:
    """Exercise early origin responses while ATS is forwarding a POST body."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("the raw delayed clients require a TCP listener")
        if not Curl.supports("http2"):
            pytest.skip("curl with HTTP/2 support is required")
        if shutil.which("nc") is None:
            pytest.skip("nc is required for the delayed POST clients")
        self._services = services
        self._curl = curl
        self._ports = [services.allocate_port() for _ in range(6)]
        self._origins = self.configure_origins(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origins(self, services: ServiceFactory) -> list[ProcessService]:
        """Create one single-use early-response origin for each transaction."""

        mock_origin = TEST_DIRECTORY.parents[1] / "tools" / "mock_origin.py"
        origins = []
        for number, port in enumerate(self._ports, 1):
            origins.append(
                services.process(
                    f"server{number}",
                    (
                        sys.executable,
                        mock_origin,
                        str(port),
                        "--status",
                        "420",
                        "--reason",
                        "Be Calm",
                        "--output",
                        f"outserver{number}",
                    ),
                    ready_port=port,
                ))
        return origins

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS and route each case to its single-use origin."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update({
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
        })
        for name, port in zip(("one", "two", "three", "four", "five", "six"), self._ports):
            ats.remap_config.add_line(f"map /{name} http://127.0.0.1:{port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        return ats

    def run_curl_case(self, protocol: str, path: str, body: str) -> None:
        """POST @a body with curl and require the early origin response."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--verbose --output /dev/null '--{protocol}' --header Expect: --data '{body}' --insecure "
                f"'https://127.0.0.1:{self._ats.https_port}/{path}'"),
            timeout=30,
        )
        assert result.returncode == 0, result.output
        expected = "HTTP/2 420" if protocol == "http2" else "HTTP/1.1 420 Be Calm"
        assert expected in result.output

    def run_delayed_case(self, number: int, output_name: str) -> None:
        """Run one raw client that pauses before completing its request body."""

        output = self._ats.run_directory.parent / output_name
        suffix = "" if number == 1 else str(number)
        client = self._services.process(
            f"client{number}",
            (
                "sh",
                TEST_DIRECTORY / f"delay_client{suffix}.sh",
                str(self._ats.http_port),
                output,
            ),
        )
        result = client.run(timeout=15)
        assert result.returncode == 0, result.output
        response = output.read_text(errors="replace")
        assert "0123456789" not in response
        assert "HTTP/1.1 420 Be Calm" in response
        assert "Connection: close" in response

    def run(self) -> None:
        """Run ordinary and deliberately paused POST bodies against ATS."""

        for origin in self._origins:
            origin.start()
        self._ats.start()
        body = self._ats.run_directory.parent / "big_post_body"
        body.write_text("0123456789" * 231070)

        self.run_curl_case("http1.1", "one", "small body")
        self.run_curl_case("http1.1", "two", f"@{body}")
        self.run_curl_case("http2", "three", f"@{body}")
        self.run_delayed_case(1, "clientout")
        self.run_delayed_case(2, "clientout2")
        self.run_delayed_case(3, "clientout3")


def test_post_early_return(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns an early origin response without forwarding the remaining body."""

    PostEarlyReturnScenario(ats_factory, services, curl).run()
