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


def run_remap_web_socket(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, *, use_yaml: bool) -> None:
    """Verify WebSocket upgrade remapping and tunnel metrics.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_yaml: Use yaml used by this test step.
    """

    __METRICS = (
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

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create an origin that accepts one WebSocket upgrade.

        :param services: Factory owning support services and their cleanup.
        """

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

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure equivalent classic or YAML ws and wss mappings.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        if use_yaml:
            ats.remap_yaml.add_lines(
                [
                    "remap:",
                    "  - type: map",
                    f"    from: {{url: 'ws://www.example.com:{ats.http_port}'}}",
                    f"    to: {{url: 'ws://127.0.0.1:{_origin.port}'}}",
                    "  - type: map",
                    f"    from: {{url: 'wss://www.example.com:{ats.https_port}'}}",
                    f"    to: {{url: 'ws://127.0.0.1:{_origin.port}'}}",
                ])
        else:
            ats.remap_config.add_lines(
                [
                    f"map ws://www.example.com:{ats.http_port} ws://127.0.0.1:{_origin.port}",
                    f"map wss://www.example.com:{ats.https_port} ws://127.0.0.1:{_origin.port}",
                ])
        return ats

    def request_upgrade(*, tls: bool) -> None:
        """Request an upgrade and verify the successful handshake.

        :param tls: Tls used by this test step.
        """

        port = _ats.https_port if tls else _ats.http_port
        scheme = "https" if tls else "http"
        result = curl.run_for(
            _ats,
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

    _test_directory = Path(__file__).parent
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)

    _origin.start()
    _ats.start()
    if not curl.uses_uds:
        request_upgrade(tls=True)
    request_upgrade(tls=False)

    result = curl.run_for(
        _ats,
        (
            f"--max-time 2 --verbose --silent --http1.1 --header 'Connection: Upgrade' --header "
            f"'Upgrade: websocket' --resolve 'www.example.com:{_ats.http_port}:127.0.0.1' "
            f"'http://www.example.com:{_ats.http_port}/chat'"),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 400 Invalid Upgrade Request" in result.stderr, result.output

    result = _ats.traffic_ctl("metric", "get", *__METRICS)
    assert result.returncode == 0, result.output
    filename = "remap-ws-metrics-uds.gold" if curl.uses_uds else "remap-ws-metrics.gold"
    assert_matches_gold(result.stdout, _test_directory / "gold" / filename)
