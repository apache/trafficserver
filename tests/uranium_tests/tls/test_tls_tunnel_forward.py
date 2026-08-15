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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class TlsTunnelForwardScenario:
    """Blind-tunnel or TLS-forward connections according to client SNI."""

    _metrics = (
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

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._foo = self.configure_origin(services, "server_foo", "foo.com", "ok foo", ssl=True)
        self._bar = self.configure_origin(services, "server_bar", "bar.com", "ok bar")
        self._random = self.configure_origin(services, "server_random", "random.com", "ok random")
        self._dns = services.dns("dns", default="127.0.0.1")
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(
        services: ServiceFactory,
        name: str,
        host: str,
        body: str,
        *,
        ssl: bool = False,
    ) -> OriginServer:
        """Create one tunnel or forwarding destination."""

        origin = services.origin(name, ssl=ssl)
        origin.add_response(
            {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": body
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure exact tunnel, exact forward, and default forward routes."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            TEST_DIRECTORY / "ssl" / "signed-foo.pem",
            TEST_DIRECTORY / "ssl" / "signed-foo.key",
            TEST_DIRECTORY / "ssl" / "signer.pem",
        )
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: signed-foo.pem",
                "    ssl_key_name: signed-foo.key",
            ))
        ats.records.update(
            {
                "proxy.config.http.connect_ports":
                    (f"{ats.https_port} {self._foo.https_port} {self._bar.http_port} {self._random.http_port}"),
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        ats.allow_private_connect()
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: foo.com\n"
            f"    tunnel_route: localhost:{self._foo.https_port}\n"
            "  - fqdn: bar.com\n"
            f"    forward_route: localhost:{self._bar.http_port}\n"
            "  - fqdn: ''\n"
            f"    forward_route: localhost:{self._random.http_port}\n",
        )
        return ats

    def request(self, host: str | None) -> str:
        """Issue one tunnel or forwarding request."""

        arguments = ["--verbose", "--http1.1", "--insecure"]
        if host is None:
            arguments.extend(("--header", "Host: random.com"))
            url = f"https://127.0.0.1:{self._ats.https_port}/"
        else:
            arguments.extend(("--resolve", f"{host}:{self._ats.https_port}:127.0.0.1"))
            url = f"https://{host}:{self._ats.https_port}/"
        result = self._curl.run_for(self._ats, *arguments, url)
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.output
        assert "Not Found on Accelerato" not in result.output
        assert "HTTP/1.1 200 OK" in result.output
        return result.output

    def run(self) -> None:
        """Exercise each route and verify tunnel classification metrics."""

        self._foo.start()
        self._bar.start()
        self._random.start()
        self._dns.start()
        self._ats.start()

        tunneled = self.request("foo.com")
        assert "CN=foo.com" not in tunneled
        assert "ok foo" in tunneled

        forwarded = self.request("bar.com")
        assert "CN=foo.com" in forwarded
        assert "ok bar" in forwarded

        default = self.request(None)
        assert "CN=foo.com" in default
        assert "ok random" in default

        metrics = self._ats.traffic_ctl("metric", "get", *self._metrics)
        assert metrics.returncode == 0, metrics.output
        assert_matches_gold(metrics.stdout, TEST_DIRECTORY / "gold" / "tls-tunnel-forward-metrics.gold")


def test_tls_tunnel_forward(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI tunnel and forward routes terminate TLS only when configured."""

    TlsTunnelForwardScenario(ats_factory, services, curl).run()
