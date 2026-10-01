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
import shlex

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent

TLS_TUNNEL_FORWARD__metrics = (
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


def configure_origin(
    services: ServiceFactory,
    name: str,
    host: str,
    body: str,
    *,
    ssl: bool = False,
) -> OriginServer:
    """Create one tunnel or forwarding destination.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param host: HTTP host name used for the request.
    :param body: HTTP message body.
    :param ssl: Ssl used by this test step.
    """

    origin = services.origin(name, ssl=ssl)
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": body
        },
    )
    return origin


def configure_ats(
        ats_factory: ATSFactory, *, _bar: OriginServer, _dns: DNSServer, _foo: OriginServer, _random: OriginServer) -> ATS:
    """Configure exact tunnel, exact forward, and default forward routes.

    :param _bar: Test-local bar configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _foo: Test-local foo configured by the test.
    :param _random: Test-local random configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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
            "proxy.config.http.connect_ports": (f"{ats.https_port} {_foo.https_port} {_bar.http_port} {_random.http_port}"),
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.allow_private_connect()
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: foo.com\n"
        f"    tunnel_route: localhost:{_foo.https_port}\n"
        "  - fqdn: bar.com\n"
        f"    forward_route: localhost:{_bar.http_port}\n"
        "  - fqdn: ''\n"
        f"    forward_route: localhost:{_random.http_port}\n",
    )
    return ats


def request(host: str | None, *, _ats: ATS, _curl: Curl) -> str:
    """Issue one tunnel or forwarding request.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    """

    arguments = ["--verbose", "--http1.1", "--insecure"]
    if host is None:
        arguments.extend(("--header", "Host: random.com"))
        url = f"https://127.0.0.1:{_ats.https_port}/"
    else:
        arguments.extend(("--resolve", f"{host}:{_ats.https_port}:127.0.0.1"))
        url = f"https://{host}:{_ats.https_port}/"
    result = _curl.run_for(
        _ats,
        f"{shlex.join(arguments)} '{url}'",
    )
    assert result.returncode == 0, result.output
    assert "Could Not Connect" not in result.output
    assert "Not Found on Accelerato" not in result.output
    assert "HTTP/1.1 200 OK" in result.output
    return result.output


def test_tls_tunnel_forward(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI tunnel and forward routes terminate TLS only when configured.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _foo = configure_origin(services, "server_foo", "foo.com", "ok foo", ssl=True)
    _bar = configure_origin(services, "server_bar", "bar.com", "ok bar")
    _random = configure_origin(services, "server_random", "random.com", "ok random")
    _dns = services.dns("dns", default="127.0.0.1")
    _ats = configure_ats(ats_factory, _bar=_bar, _dns=_dns, _foo=_foo, _random=_random)

    _foo.start()
    _bar.start()
    _random.start()
    _dns.start()
    _ats.start()

    tunneled = request("foo.com", _ats=_ats, _curl=curl)
    assert "CN=foo.com" not in tunneled
    assert "ok foo" in tunneled

    forwarded = request("bar.com", _ats=_ats, _curl=curl)
    assert "CN=foo.com" in forwarded
    assert "ok bar" in forwarded

    default = request(None, _ats=_ats, _curl=curl)
    assert "CN=foo.com" in default
    assert "ok random" in default

    metrics = _ats.traffic_ctl("metric", "get", *TLS_TUNNEL_FORWARD__metrics)
    assert metrics.returncode == 0, metrics.output
    assert_matches_gold(metrics.stdout, TEST_DIRECTORY / "gold" / "tls-tunnel-forward-metrics.gold")
