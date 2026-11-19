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

import pytest
from tools.uranium.services import ATS, ATSFactory, DNSServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

BAD_PORT = r"bad port in tunnel destination"

# Each route names a port Traffic Server cannot connect to.
BAD_ROUTES = {
    "empty": ("forward_route", "127.0.0.1:"),
    "zero": ("forward_route", "127.0.0.1:0"),
    "above_65535": ("forward_route", "127.0.0.1:99999"),
    "too_large_for_int": ("forward_route", "127.0.0.1:99999999999"),
    "trailing_text": ("tunnel_route", "127.0.0.1:443x"),
    "misspelled_template": ("partial_blind_route", "127.0.0.1:{inbound_port}"),
    "ipv6_empty": ("tunnel_route", "[::1]:"),
}


@pytest.mark.parametrize("key,destination", BAD_ROUTES.values(), ids=BAD_ROUTES.keys())
def test_tunnel_route_bad_port_stops_startup(ats_factory: ATSFactory, key: str, destination: str) -> None:
    """An sni.yaml route with a port Traffic Server cannot connect to stops startup.

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
    result = ats.traffic_ctl("config", "reload", "-m", "-t", "bad-tunnel-port", "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode == 2, result.output
    diags = wait_for_file_lines(ats.diags_log, r"sni\.yaml failed to load", 1)
    assert BAD_PORT in diags, diags

    assert ats.is_running
    assert "This is a reply" in tunnel_reply(services, "client-after", _ats=ats, _forward_port=forward_port)
