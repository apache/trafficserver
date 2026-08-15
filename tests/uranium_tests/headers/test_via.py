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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class ViaHeaderScenario:
    """Observe the protocol stack encoded in upstream Via headers."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if not Curl.supports("http2") or not Curl.supports("IPv6"):
            pytest.skip("curl HTTP/2 and IPv6 support are required")
        self._curl = curl
        self._enable_quic = ats_factory.has_feature("TS_USE_QUIC") and Curl.supports("http3")
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Load the microserver hook that records normalized Via headers."""

        origin = services.origin(
            "server",
            options={"--load": TEST_DIRECTORY / "via-observer.py"},
        )
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable maximal Via detail on every supported listener."""

        ats = ats_factory.create("ts", enable_tls=True, enable_quic=self._enable_quic)
        records: dict[str, object] = {
            "proxy.config.http.insert_request_via_str": 4,
            "proxy.config.http.insert_response_via_str": 4,
        }
        if not self._curl.uses_uds:
            server_ports = (f"{ats.http_port} {ats.ipv6_port}:ipv6 "
                            f"{ats.https_port}:ssl {ats.ipv6_https_port}:ssl:ipv6")
            if self._enable_quic:
                server_ports += f" {ats.https_port}:quic {ats.ipv6_https_port}:quic:ipv6"
            records["proxy.config.http.server_ports"] = server_ports
        ats.records.update(records)
        ats.remap_config.add_lines(
            (
                f"map http://www.example.com http://127.0.0.1:{self._origin.port}",
                f"map https://www.example.com http://127.0.0.1:{self._origin.port}",
            ))
        return ats

    def curl(self, *arguments: str) -> None:
        """Run one client protocol variant."""

        result = self._curl.run_for(self._ats, "--verbose", *arguments)
        assert result.returncode == 0, result.output

    def run_uds_requests(self) -> int:
        """Exercise the Via stack available over a Unix socket."""

        self.curl("--http1.1", "--proxy", f"localhost:{self._ats.http_port}", "http://www.example.com")
        self.curl("--http1.0", "--proxy", f"localhost:{self._ats.http_port}", "http://www.example.com")
        self.curl("--http1.1", "--proxy", f"localhost:{self._ats.ipv6_port}", "http://www.example.com")
        return 3

    def run_network_requests(self) -> int:
        """Exercise clear-text, TLS, HTTP/2, optional HTTP/3, and IPv6."""

        self.curl("--ipv4", "--http1.1", "--proxy", f"localhost:{self._ats.http_port}", "http://www.example.com")
        self.curl("--ipv4", "--http1.0", "--proxy", f"localhost:{self._ats.http_port}", "http://www.example.com")
        self.curl(
            "--ipv4",
            "--http2",
            "--insecure",
            "--header",
            "Host: www.example.com",
            f"https://localhost:{self._ats.https_port}",
        )
        count = 3
        if self._enable_quic:
            self.curl(
                "--ipv4",
                "--http3",
                "--insecure",
                "--header",
                "Host: www.example.com",
                f"https://localhost:{self._ats.https_port}",
            )
            count += 1
        self.curl(
            "--ipv4",
            "--http1.1",
            "--insecure",
            "--header",
            "Host: www.example.com",
            f"https://localhost:{self._ats.https_port}",
        )
        self.curl("--ipv6", "--http1.1", "--proxy", f"localhost:{self._ats.ipv6_port}", "http://www.example.com")
        self.curl(
            "--ipv6",
            "--http1.1",
            "--insecure",
            "--header",
            "Host: www.example.com",
            f"https://localhost:{self._ats.ipv6_https_port}",
        )
        return count + 3

    def run(self) -> None:
        """Send each protocol variant and compare the observed Via stacks."""

        self._origin.start()
        self._ats.start()
        count = self.run_uds_requests() if self._curl.uses_uds else self.run_network_requests()
        log_path = self._origin.run_directory / "via.log"
        wait_for_file_lines(log_path, r"^Via:", count)
        gold = "via_uds.gold" if self._curl.uses_uds else ("via_h3.gold" if self._enable_quic else "via.gold")
        assert_matches_gold(log_path.read_text(errors="replace"), TEST_DIRECTORY / gold)


def test_via(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Upstream Via headers accurately describe each client protocol stack."""

    ViaHeaderScenario(ats_factory, services, curl).run()
