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

import pytest
from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

BAD_PORT = r"bad port in tunnel destination"

# Each route names a port the tunnel would misread.
BAD_ROUTES = {
    "empty": ("forward_route", "127.0.0.1:"),
    "zero": ("forward_route", "127.0.0.1:0"),
    "above_65535": ("forward_route", "127.0.0.1:99999"),
    "too_large_for_int": ("forward_route", "127.0.0.1:99999999999"),
    "trailing_text": ("tunnel_route", "127.0.0.1:443x"),
    "misspelled_template": ("partial_blind_route", "127.0.0.1:{inbound_port}"),
    "ipv6_empty": ("tunnel_route", "[::1]:"),
    "leading_zero": ("tunnel_route", "127.0.0.1:0443"),
}


@pytest.mark.parametrize("key,destination", BAD_ROUTES.values(), ids=BAD_ROUTES.keys())
def test_tunnel_route_bad_port_stops_startup(ats_factory: ATSFactory, key: str, destination: str) -> None:
    """An sni.yaml route with a port the tunnel would misread stops startup.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param key: The sni.yaml route key under test.
    :param destination: The route's destination.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.write_config_file("sni.yaml", f'sni:\n- fqdn: bar.com\n  {key}: "{destination}"\n')
    ats.expect_start_failure(rf"EMERGENCY: .*sni\.yaml failed to load: .*{BAD_PORT}")
    ats.start()


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _forward_port: int) -> ATS:
    """Route the bar.com TLS stream to a raw TCP listener.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _dns: Test-local dns configured by the test.
    :param _forward_port: Test-local forward port configured by the test.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.records.update(
        {
            "proxy.config.http.connect_ports": f"{ats.https_port} {_forward_port}",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        }
    )
    ats.allow_private_connect()
    ats.allow_errors(r"sni\.yaml failed to load")
    ats.write_config_file("sni.yaml", f"sni:\n- fqdn: bar.com\n  forward_route: localhost:{_forward_port}\n")
    return ats


def tunnel_reply(services: ServiceFactory, name: str, *, _ats: ATS, _forward_port: int) -> str:
    """Send one message through the bar.com route and return the client's output.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique name for the client process.
    :param _ats: Test-local ats configured by the test.
    :param _forward_port: Test-local forward port configured by the test.
    """

    client = services.process(name, ("sh", TEST_DIRECTORY / "test-nc-s_client.sh", str(_forward_port), str(_ats.https_port)))
    result = client.run(timeout=30)
    assert result.returncode == 0, result.output
    return result.output


def test_tunnel_route_bad_port_on_reload_keeps_old_config(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A bad route port in a reloaded sni.yaml fails the reload and keeps the old routes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    forward_port = services.allocate_port()
    dns = services.dns("dns", default="127.0.0.1")
    ats = configure_ats(ats_factory, _dns=dns, _forward_port=forward_port)

    dns.start()
    ats.start()
    assert "This is a reply" in tunnel_reply(services, "client-before", _ats=ats, _forward_port=forward_port)

    # Port 1 rather than the working port with text after it: a reload that took
    # this route would read the leading digits and keep tunnelling to the old port.
    (ats.config_directory / "sni.yaml").write_text("sni:\n- fqdn: bar.com\n  forward_route: localhost:1x\n")
    result = ats.traffic_ctl("config", "reload", "-m", "-t", "bad-tunnel-port", "-r", "0.5", "-T", "30s")
    assert result.returncode == 2, result.output
    diags = wait_for_file_lines(ats.diags_log, r"sni\.yaml failed to load", 1)
    assert BAD_PORT in diags, diags

    assert ats.is_running
    assert "This is a reply" in tunnel_reply(services, "client-after", _ats=ats, _forward_port=forward_port)


def configure_match_group_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Route an SNI to the port or the host its first labels name, and one SNI to the Proxy Protocol port.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http.connect_ports": f"{ats.https_port} {_origin.http_port}",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        }
    )
    ats.allow_private_connect()
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "- fqdn: '*.port.example'\n"
        "  forward_route: 127.0.0.1:$1\n"
        "- fqdn: '*.host.example'\n"
        "  forward_route: $1\n"
        "- fqdn: pp.example\n"
        "  forward_route: 127.0.0.1:{proxy_protocol_port}\n",
    )
    return ats


def forward_request(sni: str, *, _ats: ATS, _curl: Curl) -> tuple[int, str]:
    """Send one request through the forward route that sni selects.

    :param sni: The server name the client sends.
    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    arguments = ["--verbose", "--http1.1", "--insecure", "--resolve", f"{sni}:{_ats.https_port}:127.0.0.1"]
    result = _curl.run_for(_ats, f"{shlex.join(arguments)} 'https://{sni}:{_ats.https_port}/'")
    return result.returncode, result.output


def handshake(services: ServiceFactory, name: str, sni: str, *, _ats: ATS) -> int:
    """Complete a TLS handshake with sni, which may hold a colon, and return openssl's exit status.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique name for the client process.
    :param sni: The server name the client sends.
    :param _ats: Test-local ats configured by the test.
    """

    command = f"openssl s_client -connect 127.0.0.1:{_ats.https_port} -servername {shlex.quote(sni)} < /dev/null"
    client = services.process(name, ("sh", "-c", command))
    client.expect_return_codes(0, 1)
    return client.run(timeout=30).returncode


def test_tunnel_route_match_group_port(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A destination filled in from the SNI is used only if its port is a whole port number and it keeps its shape.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: origin\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "forwarded ok"},
    )
    ats = configure_match_group_ats(ats_factory, _origin=origin)
    origin.start()
    ats.start()

    port = origin.http_port
    returncode, output = forward_request(f"{port}.port.example", _ats=ats, _curl=curl)
    assert returncode == 0 and "forwarded ok" in output, output

    # A filled-in port must be a whole port number: the handshake fails for
    # text after the digits, a port above 65535, a leading zero and a name.
    for label in (f"{port}x", str(port + 65536), "0" + format(port, "o"), "https"):
        returncode, output = forward_request(f"{label}.port.example", _ats=ats, _curl=curl)
        assert returncode == 35, f"{label}: {output}"
        assert "forwarded ok" not in output, f"{label}: {output}"
    assert ats.is_running

    # A match group must keep the shape the destination was configured with:
    # "1:2" leaves "127.0.0.1:$1" with no port, and "127.0.0.1:<port>" gives
    # "$1" a port it was configured without. The handshake fails for both, and
    # goes on for a port that keeps the shape.
    assert handshake(services, "shape-kept", f"{port}.port.example", _ats=ats) == 0
    assert handshake(services, "port-dropped", "1:2.port.example", _ats=ats) == 1
    assert handshake(services, "port-gained", f"127.0.0.1:{port}.host.example", _ats=ats) == 1
    assert ats.is_running

    # A connection without a Proxy Protocol header has no port to fill in, so
    # the tunnel falls back to the inbound port.
    forward_request("pp.example", _ats=ats, _curl=curl)
    wait_for_file_lines(ats.traffic_out, rf"CONNECT tunnel://127\.0\.0\.1:{ats.https_port} HTTP/1\.1", 1)
