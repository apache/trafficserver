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

import socket

from tools.uranium.services import ATS, ServiceFactory


class DenyAnyAddressScenario:
    """Reject direct and redirected requests to IPv4 or IPv6 any-addresses."""

    HOST = "redirect.test"

    def __init__(self, ats: ATS, services: ServiceFactory) -> None:
        self.ats = ats
        self.services = services

    def _configure_services(self) -> None:
        self.redirect_origin = self.services.origin("redirect-origin", ip="0.0.0.0")
        self.dns = self.services.dns("dns")
        self.dns.add_records({self.HOST: ["127.0.0.1"]})

    def _configure_redirects(self) -> None:
        self.redirect_origin.add_response(
            {"headers": "GET /redirect-0 HTTP/1.1\r\nHost: *\r\n\r\n"},
            {"headers": f"HTTP/1.1 302 Found\r\nLocation: http://0:{self.ats.http_port}/\r\nConnection: close\r\n\r\n"},
        )
        self.redirect_origin.add_response(
            {"headers": "GET /redirect-0v6 HTTP/1.1\r\nHost: *\r\n\r\n"},
            {"headers": f"HTTP/1.1 302 Found\r\nLocation: http://[::]:{self.ats.http_port}/\r\nConnection: close\r\n\r\n"},
        )

    def _configure_traffic_server(self) -> None:
        self.ats.records.update(
            {
                "proxy.config.http.server_ports": f"{self.ats.http_port} {self.ats.ipv6_port}:ipv6",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|redirect",
                "proxy.config.http.number_of_redirections": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self.dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.url_remap.remap_required": 0,
            })

    def _start_services(self) -> None:
        self.redirect_origin.start()
        self.dns.start()
        self.ats.start()

    @staticmethod
    def _first_response_line(address: str, port: int, request: str) -> str:
        with socket.create_connection((address, port), timeout=5) as connection:
            connection.sendall(request.encode())
            response = b""
            while b"\r\n" not in response:
                data = connection.recv(4096)
                if not data:
                    break
                response += data
        return response.split(b"\r\n", 1)[0].decode(errors="replace")

    def _verify_rejections(self) -> None:
        requests = [
            ("127.0.0.1", self.ats.http_port, f"GET / HTTP/1.1\r\nHost: 0:{self.ats.http_port}\r\nConnection: close\r\n\r\n"),
            ("::1", self.ats.ipv6_port, f"GET / HTTP/1.1\r\nHost: [::]:{self.ats.ipv6_port}\r\nConnection: close\r\n\r\n"),
            (
                "127.0.0.1",
                self.ats.http_port,
                f"GET /redirect-0 HTTP/1.1\r\nHost: {self.HOST}:{self.redirect_origin.port}\r\n\r\n",
            ),
            (
                "127.0.0.1",
                self.ats.http_port,
                f"GET /redirect-0v6 HTTP/1.1\r\nHost: {self.HOST}:{self.redirect_origin.port}\r\n\r\n",
            ),
        ]
        for address, port, request in requests:
            assert self._first_response_line(address, port, request) == "HTTP/1.1 400 Bad Destination Address"

    def run(self) -> None:
        self._configure_services()
        self._configure_redirects()
        self._configure_traffic_server()
        self._start_services()
        self._verify_rejections()


def test_deny_any_address(ats: ATS, services: ServiceFactory) -> None:
    DenyAnyAddressScenario(ats, services).run()
