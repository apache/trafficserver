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


def test_deny_any_address(ats: ATS, services: ServiceFactory) -> None:
    """Reject direct and redirected requests to IPv4 or IPv6 any-addresses.

    :param ats: Traffic Server instance configured or queried by this step.
    :param services: Factory owning support services and their cleanup.
    """

    _HOST = "redirect.test"

    def _first_response_line(address: str, port: int, request: str) -> str:
        """first response line.

        :param address: Address used by this test step.
        :param port: Allocated TCP or UDP listener port number.
        :param request: Request used by this test step.
        """
        with socket.create_connection((address, port), timeout=5) as connection:
            connection.sendall(request.encode())
            response = b""
            while b"\r\n" not in response:
                data = connection.recv(4096)
                if not data:
                    break
                response += data
        return response.split(b"\r\n", 1)[0].decode(errors="replace")

    redirect_origin = services.origin("redirect-origin", ip="0.0.0.0")
    dns = services.dns("dns")
    dns.add_records({_HOST: ["127.0.0.1"]})
    redirect_origin.add_response(
        {"headers": "GET /redirect-0 HTTP/1.1\r\nHost: *\r\n\r\n"},
        {"headers": f"HTTP/1.1 302 Found\r\nLocation: http://0:{ats.http_port}/\r\nConnection: close\r\n\r\n"},
    )
    redirect_origin.add_response(
        {"headers": "GET /redirect-0v6 HTTP/1.1\r\nHost: *\r\n\r\n"},
        {"headers": f"HTTP/1.1 302 Found\r\nLocation: http://[::]:{ats.http_port}/\r\nConnection: close\r\n\r\n"},
    )
    ats.records.update(
        {
            "proxy.config.http.server_ports": f"{ats.http_port} {ats.ipv6_port}:ipv6",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|redirect",
            "proxy.config.http.number_of_redirections": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.url_remap.remap_required": 0,
        })
    redirect_origin.start()
    dns.start()
    ats.start()
    requests = [
        ("127.0.0.1", ats.http_port, f"GET / HTTP/1.1\r\nHost: 0:{ats.http_port}\r\nConnection: close\r\n\r\n"),
        ("::1", ats.ipv6_port, f"GET / HTTP/1.1\r\nHost: [::]:{ats.ipv6_port}\r\nConnection: close\r\n\r\n"),
        (
            "127.0.0.1",
            ats.http_port,
            f"GET /redirect-0 HTTP/1.1\r\nHost: {_HOST}:{redirect_origin.port}\r\n\r\n",
        ),
        (
            "127.0.0.1",
            ats.http_port,
            f"GET /redirect-0v6 HTTP/1.1\r\nHost: {_HOST}:{redirect_origin.port}\r\n\r\n",
        ),
    ]
    for address, port, request in requests:
        assert _first_response_line(address, port, request) == "HTTP/1.1 400 Bad Destination Address"
