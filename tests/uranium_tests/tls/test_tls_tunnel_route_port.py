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

from collections.abc import Callable
from pathlib import Path
import shlex

import pytest
from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

BAD_PORT = r"bad port in tunnel destination"
BAD_MATCH_GROUP = r"bad match group in tunnel destination"
NO_HOST = r"no host in tunnel destination"
NO_DESTINATION = r"has no tunnel destination"

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


# Each route has a match group the fqdn does not capture, or a '$' that starts none.
BAD_MATCH_GROUP_ROUTES = {
    "exact_fqdn": ("bar.com", "$1.backend.example.com:443"),
    "not_a_group": ("*.bar.com", "${1}.backend.example.com:443"),
}


@pytest.mark.parametrize("fqdn,destination", BAD_MATCH_GROUP_ROUTES.values(), ids=BAD_MATCH_GROUP_ROUTES.keys())
def test_tunnel_route_bad_match_group_stops_startup(ats_factory: ATSFactory, fqdn: str, destination: str) -> None:
    """An sni.yaml route with a match group the fqdn does not capture, or a '$' that starts none, stops startup.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param fqdn: The route's fqdn.
    :param destination: The route's destination.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.write_config_file("sni.yaml", f'sni:\n- fqdn: "{fqdn}"\n  forward_route: "{destination}"\n')
    ats.expect_start_failure(rf"EMERGENCY: .*sni\.yaml failed to load: .*{BAD_MATCH_GROUP}")
    ats.start()


# Each route key has no destination, with the error it gives. An empty one would
# load with no route installed, and a YAML null would load as the host "null".
NO_DESTINATION_ROUTES = {
    "empty": ('forward_route: ""', NO_HOST),
    "null": ("tunnel_route:", NO_DESTINATION),
    "tilde": ("partial_blind_route: ~", NO_DESTINATION),
    "tagged_null": ("forward_route: !!null", NO_DESTINATION),
}


@pytest.mark.parametrize("route,error", NO_DESTINATION_ROUTES.values(), ids=NO_DESTINATION_ROUTES.keys())
def test_tunnel_route_no_destination_stops_startup(ats_factory: ATSFactory, route: str, error: str) -> None:
    """An sni.yaml route key with an empty or null destination stops startup.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param route: The sni.yaml route key and its value.
    :param error: The loader error startup must stop with.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.write_config_file("sni.yaml", f"sni:\n- fqdn: bar.com\n  {route}\n")
    ats.expect_start_failure(rf"EMERGENCY: .*sni\.yaml failed to load: .*{error}")
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


# Each reloaded sni.yaml has a route that cannot load, with the error it gives.
BAD_RELOADS = {
    # Port 1x rather than the working port with text after it: a reload that read
    # only the leading digits of the working port would still tunnel there, and
    # the last check could not tell that the reload had been taken.
    "bad_port": ("sni:\n- fqdn: bar.com\n  forward_route: localhost:1x\n", BAD_PORT),
    "bad_match_group": ('sni:\n- fqdn: bar.com\n  forward_route: "$1.localhost:1"\n', BAD_MATCH_GROUP),
    "not_a_group": ('sni:\n- fqdn: "*.bar.com"\n  forward_route: "${1}.localhost:1"\n', BAD_MATCH_GROUP),
    "empty_route": ('sni:\n- fqdn: bar.com\n  forward_route: ""\n', NO_HOST),
    "null_route": ("sni:\n- fqdn: bar.com\n  forward_route:\n", NO_DESTINATION),
}


@pytest.mark.parametrize("sni_yaml,error", BAD_RELOADS.values(), ids=BAD_RELOADS.keys())
def test_tunnel_route_bad_reload_keeps_old_config(
    ats_factory: ATSFactory, services: ServiceFactory, sni_yaml: str, error: str
) -> None:
    """A route that cannot load in a reloaded sni.yaml fails the reload and keeps the old routes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param sni_yaml: The sni.yaml to reload.
    :param error: The loader error the reload must log.
    """

    forward_port = services.allocate_port()
    dns = services.dns("dns", default="127.0.0.1")
    ats = configure_ats(ats_factory, _dns=dns, _forward_port=forward_port)

    dns.start()
    ats.start()
    assert "This is a reply" in tunnel_reply(services, "client-before", _ats=ats, _forward_port=forward_port)

    (ats.config_directory / "sni.yaml").write_text(sni_yaml)
    result = ats.traffic_ctl("config", "reload", "-m", "-t", "bad-tunnel-route", "-r", "0.5", "-T", "30s")
    assert result.returncode == 2, result.output
    diags = wait_for_file_lines(ats.diags_log, r"sni\.yaml failed to load", 1)
    assert error in diags, diags

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


def start_match_group_ats(ats_factory: ATSFactory, services: ServiceFactory) -> tuple[ATS, int]:
    """Start an origin and a Traffic Server with the match group routes, and return ats and the origin's port.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: origin\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "forwarded ok"},
    )
    ats = configure_match_group_ats(ats_factory, _origin=origin)
    origin.start()
    ats.start()
    return ats, origin.http_port


# Labels that fill in "127.0.0.1:$1" with something that is not a whole port
# number, from the origin's port.
BAD_PORT_LABELS = {
    "trailing_text": lambda port: f"{port}x",
    "above_65535": lambda port: str(port + 65536),
    "leading_zero": lambda port: "0" + format(port, "o"),
    "name": lambda port: "https",
}


@pytest.mark.parametrize("label", BAD_PORT_LABELS.values(), ids=BAD_PORT_LABELS.keys())
def test_tunnel_route_match_group_port(
    ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, label: Callable[[int], str]
) -> None:
    """A port filled in from the SNI is used only if it is a whole port number from 1 to 65535.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param label: Makes the first SNI label from the origin's port.
    """

    ats, port = start_match_group_ats(ats_factory, services)
    returncode, output = forward_request(f"{port}.port.example", _ats=ats, _curl=curl)
    assert returncode == 0 and "forwarded ok" in output, output

    bad = label(port)
    returncode, output = forward_request(f"{bad}.port.example", _ats=ats, _curl=curl)
    assert returncode == 35, f"{bad}: {output}"
    assert "forwarded ok" not in output, f"{bad}: {output}"
    assert ats.is_running


# A match group must keep the shape the destination was configured with: "1:2"
# leaves "127.0.0.1:$1" with no port, and "127.0.0.1:<port>" gives "$1" a port
# it was configured without. Each SNI is made from the origin's port, with the
# handshake's expected openssl exit status.
SHAPE_CASES = {
    "port_kept": (lambda port: f"{port}.port.example", 0),
    "port_dropped": (lambda port: "1:2.port.example", 1),
    "port_gained": (lambda port: f"127.0.0.1:{port}.host.example", 1),
}


@pytest.mark.parametrize("sni,returncode", SHAPE_CASES.values(), ids=SHAPE_CASES.keys())
def test_tunnel_route_match_group_keeps_shape(
    ats_factory: ATSFactory, services: ServiceFactory, sni: Callable[[int], str], returncode: int
) -> None:
    """The handshake fails when a match group changes whether the destination names a port.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param sni: Makes the server name from the origin's port.
    :param returncode: The openssl exit status the handshake must give.
    """

    ats, port = start_match_group_ats(ats_factory, services)
    assert handshake(services, "client", sni(port), _ats=ats) == returncode
    assert ats.is_running


def test_tunnel_route_proxy_protocol_port_without_header(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A connection without a Proxy Protocol header has no port to fill in, so the tunnel falls back to the inbound port.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    ats, _ = start_match_group_ats(ats_factory, services)
    forward_request("pp.example", _ats=ats, _curl=curl)
    wait_for_file_lines(ats.traffic_out, rf"CONNECT tunnel://127\.0\.0\.1:{ats.https_port} HTTP/1\.1", 1)
