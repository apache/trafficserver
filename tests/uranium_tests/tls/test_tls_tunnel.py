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
import re
import sys

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    DNSServer,
    OriginServer,
    ProcessService,
    ServiceFactory,
    assert_matches_gold,
)

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"
PROXY_PROTOCOL_CLIENT = TEST_DIRECTORY / "proxy_protocol_client.py"
SPLIT_CLIENT = TEST_DIRECTORY / "split_client_hello.py"
SPLIT_SERVER = TEST_DIRECTORY / "receive_split_client_hello.py"


def configure_origin(services: ServiceFactory, name: str, host: str, body: str) -> OriginServer:
    """Create an HTTPS microserver used as a blind-tunnel destination.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param host: HTTP host name used for the request.
    :param body: HTTP message body.
    """

    origin = services.origin(name, ssl=True)
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": body
        },
    )
    origin.add_response(
        {"headers": "GET /proxy_protocol HTTP/1.1\r\nHost: proxy.protocol.port.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "pp ok"
        },
    )
    return origin


def configure_split_server(services: ServiceFactory, *, _split_port: int) -> ProcessService:
    """Create the bespoke server that verifies fragmented ClientHello input.

    :param _split_port: Test-local split port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "split-client-hello-server",
        [sys.executable, SPLIT_SERVER, "127.0.0.1", str(_split_port)],
        ready_port=_split_port,
    )


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve every expanded SNI tunnel destination to loopback.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records(
        {
            "localhost": ["127.0.0.1"],
            "one.testmatch": ["127.0.0.1"],
            "two.example.one": ["127.0.0.1"],
            "backend.incoming.port.com": ["127.0.0.1"],
            "backend.proxy.protocol.port.com": ["127.0.0.1"],
            "backend.wildcard.with.incoming.port.com": ["127.0.0.1"],
            "backend.wildcard.with.proxy.protocol.port.com": ["127.0.0.1"],
        })
    return dns


def configure_ats(
        ats_factory: ATSFactory, *, _dns: DNSServer, _server_bar: OriginServer, _server_foo: OriginServer, _split_port: int) -> ATS:
    """Configure SNI tunnel routes and the dynamic-port restrictions.

    :param _dns: Test-local dns configured by the test.
    :param _server_bar: Test-local server bar configured by the test.
    :param _server_foo: Test-local server foo configured by the test.
    :param _split_port: Test-local split port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_proxy_protocol=True)
    ats.copy_to_ssl(
        SSL_DIRECTORY / "signed-foo.pem",
        SSL_DIRECTORY / "signed-foo.key",
        SSL_DIRECTORY / "signed-bar.pem",
        SSL_DIRECTORY / "signed-bar.key",
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "signer.pem",
        SSL_DIRECTORY / "signer.key",
    )
    ats.set_ssl_multicert_yaml(
        {"ssl_multicert": [{
            "dest_ip": "*",
            "ssl_cert_name": "signed-foo.pem",
            "ssl_key_name": "signed-foo.key"
        }]})
    ats.records.update(
        {
            "proxy.config.http.connect_ports": (f"{ats.https_port} {_server_foo.https_port} {_server_bar.https_port}"),
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ssl|proxyprotocol",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    ats.allow_private_connect()
    ats.write_config_file(
        "sni.yaml", initial_sni_document(ats, _server_bar=_server_bar, _server_foo=_server_foo, _split_port=_split_port))
    return ats


def initial_sni_document(ats: ATS, *, _server_bar: OriginServer, _server_foo: OriginServer, _split_port: int) -> str:
    """Render the initial tunnel policy with the allocated service ports.

    :param _server_bar: Test-local server bar configured by the test.
    :param _server_foo: Test-local server foo configured by the test.
    :param _split_port: Test-local split port configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    return (
        "sni:\n"
        "  - fqdn: foo.com\n"
        f"    tunnel_route: localhost:{_server_foo.https_port}\n"
        "  - fqdn: slashdot.org\n"
        f"    tunnel_route: 127.0.0.1:{_split_port}\n"
        "  - fqdn: '*.bar.com'\n"
        f"    tunnel_route: localhost:{_server_foo.https_port}\n"
        "  - fqdn: '*.match.com'\n"
        f"    tunnel_route: $1.testmatch:{_server_foo.https_port}\n"
        "  - fqdn: '*.ok.two.com'\n"
        f"    tunnel_route: two.example.$1:{_server_foo.https_port}\n"
        "  - fqdn: ''\n"
        f"    tunnel_route: localhost:{_server_bar.https_port}\n"
        "  - fqdn: incoming.port.com\n"
        "    tunnel_route: backend.incoming.port.com:{inbound_local_port}\n"
        "  - fqdn: proxy.protocol.port.com\n"
        "    tunnel_route: backend.proxy.protocol.port.com:{proxy_protocol_port}\n"
        "  - fqdn: '*.backend.incoming.port.com'\n"
        "    tunnel_route: backend.$1.incoming.port.com:{inbound_local_port}\n"
        "  - fqdn: '*.with.incoming.port.com'\n"
        "    tunnel_route: backend.$1.with.incoming.port.com:{inbound_local_port}\n"
        "  - fqdn: '*.with.proxy.protocol.port.com'\n"
        "    tunnel_route: backend.$1.with.proxy.protocol.port.com:{proxy_protocol_port}\n")


def curl_request(host: str, *, expected: int = 0, use_address: bool = False, _ats: ATS, _curl: Curl) -> str:
    """Send one HTTPS request and return curl's combined diagnostic output.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param expected: Expected result for this case.
    :param use_address: Use address used by this test step.
    """

    arguments = ["--verbose", "--insecure"]
    if not use_address:
        arguments.extend(("--resolve", f"{host}:{_ats.https_port}:127.0.0.1"))
    url_host = "127.0.0.1" if use_address else host
    arguments.append(f"https://{url_host}:{_ats.https_port}")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=10,
    )
    assert result.returncode == expected, result.output
    return result.output


def proxy_protocol_request(
        name: str, sni: str, destination_port: int, expected: int, *, _ats: ATS, _services: ServiceFactory) -> str:
    """Run the bespoke TLS client with a Proxy Protocol v2 destination port.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param sni: Sni used by this test step.
    :param destination_port: Allocated destination listener port number.
    :param expected: Expected result for this case.
    """

    client = _services.process(
        name,
        [
            sys.executable,
            PROXY_PROTOCOL_CLIENT,
            "127.0.0.1",
            str(_ats.proxy_protocol_https_port),
            sni,
            "127.0.0.1",
            "127.0.0.1",
            "60123",
            str(destination_port),
            "2",
            "--https",
        ],
    )
    client.expect_return_codes(expected)
    return client.run(timeout=10).output


def split_client_hello(name: str, split_size: int | None = None, *, _ats: ATS, _services: ServiceFactory) -> None:
    """Send the recorded ClientHello whole or in fragments through ATS.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param split_size: ClientHello fragment size in bytes, or None to send it whole.
    """

    command = [sys.executable, SPLIT_CLIENT, "127.0.0.1", str(_ats.https_port)]
    if split_size is not None:
        command.extend(("--split_size", str(split_size)))
    result = _services.process(name, command).run(timeout=10)
    assert "dummy SERVER_HELLO" in result.output
    assert "data: 0" in result.output
    assert "data: 1" in result.output


def assert_tunneled(output: str, body: str) -> None:
    """Require a successful blind-tunnel response from an origin.

    :param output: Output used by this test step.
    :param body: HTTP message body.
    """

    assert "Could Not Connect" not in output
    assert "Not Found on Accelerato" not in output
    assert "HTTP/1.1 200 OK" in output
    assert "ATS" not in output
    assert body in output


def verify_initial_routes(
        *, _ats: ATS, _curl: Curl, _server_foo: OriginServer, _server_forbidden: OriginServer, _services: ServiceFactory) -> None:
    """Verify literal, wildcard, substitution, empty-SNI, and dynamic-port routes.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _server_foo: Test-local server foo configured by the test.
    :param _server_forbidden: Test-local server forbidden configured by the test.
    :param _services: Test-local services configured by the test.
    """

    assert_tunneled(curl_request("foo.com", _ats=_ats, _curl=_curl), "foo ok")
    assert_tunneled(curl_request("bob.bar.com", _ats=_ats, _curl=_curl), "foo ok")
    terminated = curl_request("bar.com", _ats=_ats, _curl=_curl)
    assert "Not Found on Accelerato" in terminated
    assert "ATS" in terminated
    assert_tunneled(curl_request("unused", use_address=True, _ats=_ats, _curl=_curl), "bar ok")
    assert_tunneled(curl_request("one.match.com", _ats=_ats, _curl=_curl), "foo ok")
    assert_tunneled(curl_request("one.ok.two.com", _ats=_ats, _curl=_curl), "foo ok")

    curl_request("incoming.port.com", expected=35, _ats=_ats, _curl=_curl)
    allowed = proxy_protocol_request(
        "proxy-protocol-allowed", "proxy.protocol.port.com", _server_foo.https_port, 0, _ats=_ats, _services=_services)
    assert "HTTP/1.1 200 OK" in allowed
    rejected = proxy_protocol_request(
        "proxy-protocol-rejected", "proxy.protocol.port.com", _server_forbidden.https_port, 1, _ats=_ats, _services=_services)
    assert re.search(r"ssl\.SSL.*Error:.*EOF", rejected)

    curl_request("wildcard.with.incoming.port.com", expected=35, _ats=_ats, _curl=_curl)
    wildcard_allowed = proxy_protocol_request(
        "wildcard-proxy-allowed",
        "wildcard.with.proxy.protocol.port.com",
        _server_foo.https_port,
        0,
        _ats=_ats,
        _services=_services)
    assert "HTTP/1.1 200 OK" in wildcard_allowed
    wildcard_rejected = proxy_protocol_request(
        "wildcard-proxy-rejected",
        "wildcard.with.proxy.protocol.port.com",
        _server_forbidden.https_port,
        1,
        _ats=_ats,
        _services=_services)
    assert re.search(r"ssl\.SSL.*Error:.*EOF", wildcard_rejected)


def reload_sni_policy(*, _ats: ATS, _server_bar: OriginServer) -> None:
    """Replace the SNI file and wait for its configuration task to succeed.

    :param _ats: Test-local ats configured by the test.
    :param _server_bar: Test-local server bar configured by the test.
    """

    (_ats.config_directory / "sni.yaml").write_text(
        "sni:\n"
        "  - fqdn: bar.com\n"
        f"    tunnel_route: localhost:{_server_bar.https_port}\n")
    result = _ats.traffic_ctl("config", "reload", "-m", "-t", "tls-tunnel-reload", "-w", "0.1", "-r", "0.2", "-T", "30s")
    assert result.returncode == 0, result.output


def verify_logs_and_metrics(*, _ats: ATS, _server_foo: OriginServer, _server_forbidden: OriginServer) -> None:
    """Check tunnel expansion diagnostics, rejections, and connection metrics.

    :param _ats: Test-local ats configured by the test.
    :param _server_foo: Test-local server foo configured by the test.
    :param _server_forbidden: Test-local server forbidden configured by the test.
    """

    output = _ats.traffic_out.read_text(errors="replace")
    assert f"CONNECT tunnel://backend.incoming.port.com:{_ats.https_port} HTTP/1.1" in output
    assert "HTTP/1.1 400 Cycle Detected" in output
    assert f"Rejected a tunnel to port {_server_forbidden.https_port} not in connect_ports" in output
    assert f"Destination now is [backend.wildcard.with.incoming.port.com:{_ats.https_port}]" in output
    assert (f"Destination now is [backend.wildcard.with.proxy.protocol.port.com:{_server_foo.https_port}]" in output)

    names = [line.split()[0] for line in (TEST_DIRECTORY / "gold/tls-tunnel-metrics.gold").read_text().splitlines()]
    metrics = _ats.traffic_ctl("metric", "get", *names)
    assert metrics.returncode == 0, metrics.output
    assert_matches_gold(metrics.stdout, TEST_DIRECTORY / "gold/tls-tunnel-metrics.gold")


def test_tls_tunnel(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI tunnel routing handles all supported destination expansion modes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _server_foo = configure_origin(services, "server-foo", "foo.com", "foo ok")
    _server_bar = configure_origin(services, "server-bar", "bar.com", "bar ok")
    _server_forbidden = configure_origin(services, "server-forbidden", "proxy.protocol.port.com", "pp ok")
    _split_port = services.allocate_port()
    _split_server = configure_split_server(services, _split_port=_split_port)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _server_bar=_server_bar, _server_foo=_server_foo, _split_port=_split_port)

    _server_foo.start()
    _server_bar.start()
    _server_forbidden.start()
    _dns.start()
    _split_server.start()
    _ats.start()
    verify_initial_routes(_ats=_ats, _curl=curl, _server_foo=_server_foo, _server_forbidden=_server_forbidden, _services=services)
    split_client_hello("whole-client-hello", split_size=0, _ats=_ats, _services=services)
    split_client_hello("split-client-hello", _ats=_ats, _services=services)
    reload_sni_policy(_ats=_ats, _server_bar=_server_bar)
    terminated = curl_request("foo.com", _ats=_ats, _curl=curl)
    assert "Not Found on Accelerato" in terminated
    assert "ATS" in terminated
    assert_tunneled(curl_request("bar.com", _ats=_ats, _curl=curl), "bar ok")
    verify_logs_and_metrics(_ats=_ats, _server_foo=_server_foo, _server_forbidden=_server_forbidden)
