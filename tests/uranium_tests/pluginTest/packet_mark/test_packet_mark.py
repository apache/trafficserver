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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SOCK_OPT_FLAG_PACKET_MARK = 0x11
SET_MARK = 0x0000000A
SEED_MARK = 0x0000FF00


def can_set_so_mark() -> bool:
    """Return whether this process may set Linux SO_MARK."""

    if not hasattr(socket, "SO_MARK"):
        return False
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, 1)
        return True
    except (OSError, PermissionError):
        return False


class PacketMarkScenario:
    """Apply and read back client- or server-side socket marks."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, side: str) -> None:
        self._curl = curl
        self._side = side
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create enough origin transactions for both server-side cases."""

        origin = services.origin("origin")
        request = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "body": ""}
        response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": ""}
        origin.add_response(request, response)
        origin.add_response(request, response)
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Seed the relevant socket option and load its API test plugin."""

        ats = ats_factory.create("ts", enable_cache=False)
        record_suffix = "in" if self._side == "client" else "out"
        ats.records.update(
            {
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.admin.user_id": "#-1",
                f"proxy.config.net.sock_packet_mark_{record_suffix}": SEED_MARK,
                f"proxy.config.net.sock_option_flag_{record_suffix}": SOCK_OPT_FLAG_PACKET_MARK,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": f"http|{self._side}_packet_mark",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.copy_custom_plugin(f"{{AtsTestPluginsDir}}/{self._side}_packet_mark.so")
        ats.plugin_config.add_line(f"{self._side}_packet_mark.so")
        return ats

    def request(self, set_header: str) -> None:
        """Set a mark and require the plugin to echo the observed value."""

        result = self._curl.run_for(
            self._ats,
            f"--verbose --ipv4 --header '{set_header}: 0x{SET_MARK:08x}' 'http://localhost:{self._ats.http_port}/'",
        )
        assert result.returncode == 0, result.output
        assert f"X-{self._side.title()}-Packet-Mark: 0x{SET_MARK:08x}".lower() in result.output.lower()

    def run(self) -> None:
        """Exercise the live socket and, for origins, the preconnect seed."""

        self._origin.start()
        self._ats.start()
        self.request("X-Set-Mark")
        if self._side == "server":
            self.request("X-Set-Mark-Preconnect")


@pytest.mark.parametrize("side", ("client", "server"))
def test_packet_mark(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, side: str) -> None:
    """The packet-mark APIs update the corresponding live socket."""

    if not can_set_so_mark():
        pytest.skip("SO_MARK requires Linux with CAP_NET_ADMIN or CAP_NET_RAW")
    PacketMarkScenario(ats_factory, services, curl, side).run()
