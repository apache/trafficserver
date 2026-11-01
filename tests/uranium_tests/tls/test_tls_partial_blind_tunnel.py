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

PARTIAL_BLIND_TUNNEL__metrics = (
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
    """Create the TLS origin reached through the partial blind route.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", ssl=True)
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "ok bar"
        },
    )
    return origin


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve partial-blind route names to loopback.

    :param services: Factory owning support services and their cleanup.
    """

    return services.dns("dns", default="127.0.0.1")


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Configure `foo.com` as a partial blind route to the TLS origin.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "signed-foo.pem",
        TEST_DIRECTORY / "ssl" / "signed-foo.key",
        TEST_DIRECTORY / "ssl" / "signed-bar.pem",
        TEST_DIRECTORY / "ssl" / "signed-bar.key",
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
            "proxy.config.http.connect_ports": f"{ats.https_port} {_origin.https_port}",
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.allow_private_connect()
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: foo.com\n"
        f"    partial_blind_route: localhost:{_origin.https_port}\n",
    )
    return ats


def test_tls_partial_blind_tunnel(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Partial blind routing forwards TLS and updates only its metrics.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _origin=_origin)

    _origin.start()
    _dns.start()
    _ats.start()
    response = curl.run_for(
        _ats,
        (f"--http1.1 --verbose --resolve 'foo.com:{_ats.https_port}:127.0.0.1' --insecure "
         f"'https://foo.com:{_ats.https_port}'"),
    )
    assert response.returncode == 0, response.output
    assert "HTTP/1.1 200 OK" in response.stderr
    assert response.stdout == "ok bar"

    metrics = _ats.traffic_ctl("metric", "get", *PARTIAL_BLIND_TUNNEL__metrics)
    assert metrics.returncode == 0, metrics.output
    assert_matches_gold(metrics.stdout, TEST_DIRECTORY / "gold" / "tls-partial-blind-tunnel-metrics.gold")
