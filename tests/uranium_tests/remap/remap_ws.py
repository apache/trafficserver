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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold


class RemapWebSocketScenario:
    """Verify WebSocket upgrade remapping and tunnel metrics."""

    _METRICS = (
        "proxy.process.http.total_incoming_connections",
        "proxy.process.http.total_client_connections",
        "proxy.process.http.total_client_connections_ipv4",
        "proxy.process.http.total_client_connections_ipv6",
        "proxy.process.http.total_server_connections",
        "proxy.process.http2.total_client_connections",
        "proxy.process.http.connect_requests",
        "proxy.process.tunnel.total_client_connections_blind_tcp",
        "proxy.process.tunnel.current_client_connections_blind_tcp",
        "proxy.process.tunnel.total_server_connections_blind_tcp",
        "proxy.process.tunnel.current_server_connections_blind_tcp",
        "proxy.process.tunnel.total_client_connections_tls_tunnel",
        "proxy.process.tunnel.current_client_connections_tls_tunnel",
        "proxy.process.tunnel.total_client_connections_tls_forward",
        "proxy.process.tunnel.current_client_connections_tls_forward",
        "proxy.process.tunnel.total_client_connections_tls_partial_blind",
        "proxy.process.tunnel.current_client_connections_tls_partial_blind",
        "proxy.process.tunnel.total_client_connections_tls_http",
        "proxy.process.tunnel.current_client_connections_tls_http",
        "proxy.process.tunnel.total_server_connections_tls",
        "proxy.process.tunnel.current_server_connections_tls",
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
        self._curl = curl
        self._use_yaml = use_yaml
        self._test_directory = Path(__file__).parent
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create an origin that accepts one WebSocket upgrade."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /chat HTTP/1.1\r\nHost: www.example.com\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
                "body": "",
            },
            {
                "headers":
                    "HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
                "body": "",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure equivalent classic or YAML ws and wss mappings."""

        ats = ats_factory.create("ts", enable_tls=True)
        if self._use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    f"    from: {{url: 'ws://www.example.com:{ats.http_port}'}}",
                    f"    to: {{url: 'ws://127.0.0.1:{self._origin.port}'}}",
                    "  - type: map",
                    f"    from: {{url: 'wss://www.example.com:{ats.https_port}'}}",
                    f"    to: {{url: 'ws://127.0.0.1:{self._origin.port}'}}",
                ])
        else:
            ats.remap_config.add_lines(
                [
                    f"map ws://www.example.com:{ats.http_port} ws://127.0.0.1:{self._origin.port}",
                    f"map wss://www.example.com:{ats.https_port} ws://127.0.0.1:{self._origin.port}",
                ])
        return ats

    def request_upgrade(self, *, tls: bool) -> None:
        """Request an upgrade and verify the successful handshake."""

        port = self._ats.https_port if tls else self._ats.http_port
        scheme = "https" if tls else "http"
        result = self._curl.run_for(
            self._ats,
            (
                f"--max-time 2 --verbose --silent --http1.1 --insecure --header 'Connection: Upgrade' --header "
                f"'Upgrade: websocket' --header 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' --header "
                f"'Sec-WebSocket-Version: 13' --resolve 'www.example.com:{port}:127.0.0.1' "
                f"'{scheme}://www.example.com:{port}/chat'"),
            timeout=10,
        )
        assert result.returncode == 28, result.output
        assert "HTTP/1.1 101 Switching Protocols" in result.stderr, result.output
        assert "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in result.stderr, result.output

    def request_invalid_upgrade(self) -> None:
        """Verify ATS rejects a handshake missing the WebSocket key."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--max-time 2 --verbose --silent --http1.1 --header 'Connection: Upgrade' --header "
                f"'Upgrade: websocket' --resolve 'www.example.com:{self._ats.http_port}:127.0.0.1' "
                f"'http://www.example.com:{self._ats.http_port}/chat'"),
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 400 Invalid Upgrade Request" in result.stderr, result.output

    def verify_metrics(self) -> None:
        """Verify connection accounting after the upgrade requests."""

        result = self._ats.traffic_ctl("metric", "get", *self._METRICS)
        assert result.returncode == 0, result.output
        filename = "remap-ws-metrics-uds.gold" if self._curl.uses_uds else "remap-ws-metrics.gold"
        assert_matches_gold(result.stdout, self._test_directory / "gold" / filename)

    def run(self) -> None:
        """Run the complete WebSocket remapping scenario."""

        self._origin.start()
        self._ats.start()
        if not self._curl.uses_uds:
            self.request_upgrade(tls=True)
        self.request_upgrade(tls=False)
        self.request_invalid_upgrade()
        self.verify_metrics()
