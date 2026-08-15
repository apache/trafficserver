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
import time

from tools.uranium.services import (
    ATS,
    ATSFactory,
    OriginServer,
    ProcessService,
    ServiceFactory,
    assert_matches_gold,
    send_tcp,
)

TEST_DIRECTORY = Path(__file__).parent


class BadHttpFormatScenario:
    """Exercise tolerant request-line parsing, version errors, and method ACLs."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._method_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._method_server = self.configure_method_server(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create responses for the requests that ATS must normalize and forward."""

        origin = services.origin("server")
        for path, request_id, response_id in (
            ("/sdfsdf/0", "0", "1"),
            ("/sdfsdf/1", "1", "2"),
            ("/example/1", "6", "3"),
            ("/example/2", "7", "4"),
        ):
            origin.add_response(
                {"headers": f"GET {path} HTTP/1.1\r\nX-Req-Id: {request_id}\r\nHost: example.com\r\n\r\n"},
                {"headers": f"HTTP/1.1 200 OK\r\nX-Resp-Id: {response_id}\r\nConnection: close\r\n\r\n"},
            )
        return origin

    def configure_method_server(self, services: ServiceFactory) -> ProcessService:
        """Create the one-shot origin that accepts an arbitrary HTTP method."""

        return services.process(
            "method-server",
            ["bash", TEST_DIRECTORY / "method-server.sh",
             str(self._method_port), "outserver"],
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable strict URI parsing with distinct IPv4 and IPv6 method ACLs."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.http.server_ports": f"{ats.http_port} {ats.ipv6_port}:ipv6",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns",
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.http.strict_uri_parsing": 1,
                "proxy.config.http.cache.http": 0,
            })
        ats.write_config_file(
            "ip_allow.yaml",
            "ip_allow:\n"
            "  - apply: in\n"
            "    ip_addrs: 127.0.0.1\n"
            "    action: allow\n"
            "    methods: [GET, xyzxyz]\n"
            "  - apply: in\n"
            "    ip_addrs: ::1\n"
            "    action: allow\n"
            "    methods: [GET]\n",
        )
        ats.remap_config.add_lines(
            (
                f"map /add-method http://127.0.0.1:{self._method_port}/",
                f"map / http://127.0.0.1:{self._origin.port}/",
            ))
        return ats

    @staticmethod
    def filtered_response(response: str) -> str:
        """Extract the stable response lines represented by client.gold."""

        lines = [
            line for line in response.replace("\r\n", "\n").splitlines()
            if line.startswith("HTTP/") or "X-Resp-Id:" in line or "<HTML>" in line
        ]
        return "\n".join(lines) + ("\n" if lines else "") + "======\n"

    def request(self, data: str, *, ipv6: bool = False) -> str:
        """Send one deliberately handcrafted request to ATS."""

        return send_tcp(
            self._ats.ipv6_port if ipv6 else self._ats.http_port,
            data,
            address="::1" if ipv6 else "127.0.0.1",
        )

    def run(self) -> None:
        """Run the malformed request matrix and compare client and origin gold files."""

        self._origin.start()
        self._ats.start()
        common = "Host: example.com\r\nConnection: close\r\n"
        requests = (
            f"GET /sdfsdf/0HTTP/1.0\r\n{common}X-Req-Id: 1\r\n\r\n",
            f"GET /sdfsdf/1HTTP/1.1\r\n{common}X-Req-Id: 2\r\n\r\n",
            f"GET /sdfsdf<HTTP/1.1\r\n{common}X-Req-Id: 2\r\n\r\n",
            f"GET /sdfsdf HTTP/1.2\r\n{common}X-Req-Id: 3\r\n\r\n",
            f"GET /sdfsdf HTTP/0.9\r\n{common}X-Req-Id: 4\r\n\r\n",
            f"GET /sdfsdf HTTP/0.9\r\n{common}X-Req-Id: 5\r\n\r\n",
        )
        client_output = "".join(self.filtered_response(self.request(request)) for request in requests)

        self._method_server.start()
        time.sleep(0.1)
        arbitrary = f"xyzxyz /add-method HTTP/1.1\r\n{common}X-Req-Id: 6\r\n\r\n"
        client_output += self.filtered_response(self.request(arbitrary))
        valid_ipv6 = f"GET /example/2 HTTP/1.1\r\n{common}X-Req-Id: 7\r\n\r\n"
        client_output += self.filtered_response(self.request(valid_ipv6, ipv6=True))
        denied_ipv6 = f"xyzxyz /example/1 HTTP/1.1\r\n{common}\r\n"
        client_output += self.filtered_response(self.request(denied_ipv6, ipv6=True))

        assert_matches_gold(client_output, TEST_DIRECTORY / "client.gold")
        method_result = self._method_server.wait(timeout=10)
        assert method_result.returncode == 0
        assert_matches_gold(
            (self._method_server.run_directory / "outserver").read_text(errors="replace"),
            TEST_DIRECTORY / "server.gold",
        )


def test_bad_http_fmt(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS handles historical request-line edge cases without corrupting later requests."""

    BadHttpFormatScenario(ats_factory, services).run()
