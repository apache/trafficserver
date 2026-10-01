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

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the TLS endpoint used by permitted tunnels.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", ssl=True)
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{origin.https_port}\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, name: str, *, allow_loopback: bool = False, _origin: OriginServer) -> ATS:
    """Configure one forward proxy and optional loopback outbound allow.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param allow_loopback: Allow loopback used by this test step.
    """

    ats = ats_factory.create(f"ts-{name}", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ip_allow",
            "proxy.config.http.connect_ports": str(_origin.https_port),
            "proxy.config.url_remap.remap_required": 0,
        })
    if allow_loopback:
        ats.ip_allow_config.add_lines(
            (
                "ip_allow:",
                "  - apply: in",
                "    ip_addrs: 127.0.0.1",
                "    action: allow",
                "    methods: ALL",
                "  - apply: out",
                "    ip_addrs: 127.0.0.1",
                "    action: allow",
                "    methods: CONNECT",
                "  - apply: out",
                "    ip_addrs:",
                "      - 0.0.0.0/8",
                "      - 127.0.0.0/8",
                '      - "::"',
                "      - ::1",
                "      - 10.0.0.0/8",
                "      - 172.16.0.0/12",
                "      - 192.168.0.0/16",
                "      - 169.254.0.0/16",
                "      - ::/96",
                "      - fc00::/7",
                "      - fe80::/10",
                "      - ::ffff:0:0/96",
                "    action: deny",
                "    methods: CONNECT",
            ))
    return ats


def configure_sni_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure an SNI tunnel route to the prohibited loopback target.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-sni-default", enable_cache=False, enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ip_allow|ssl|sni",
            "proxy.config.http.connect_ports": str(_origin.https_port),
        })
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: sni-denied.example.com\n"
        f"    tunnel_route: 127.0.0.1:{_origin.https_port}\n",
    )
    return ats


def connect(ats: ATS, url: str, *, _curl: Curl) -> CommandResult:
    """Attempt one CONNECT request and report curl's status fields.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param url: Url used by this test step.
    """

    return _curl.run_for(
        ats,
        (
            f"--silent --insecure --noproxy does-not-match --proxy 'http://127.0.0.1:{ats.http_port}' --output "
            f"/dev/null --write-out 'http_code=%{{http_code}} http_connect=%{{http_connect}}\n' '{url}'"),
    )


def verify_denied_targets(*, _curl: Curl, _default: ATS, _origin: OriginServer) -> None:
    """Verify prohibited and syntactically invalid CONNECT targets.

    :param _curl: Test-local curl configured by the test.
    :param _default: Test-local default configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    port = _origin.https_port
    cases = (
        (f"https://127.0.0.1:{port}/", "403"),
        (f"https://0.0.0.0:{port}/", "400"),
        (f"https://0.1.2.3:{port}/", "403"),
        (f"https://[::]:{port}/", "400"),
        (f"https://[::7f00:1]:{port}/", "403"),
        (f"https://[::ffff:127.0.0.1]:{port}/", "403"),
        (f"https://[::ffff:7f00:1]:{port}/", "403"),
    )
    for url, status in cases:
        result = connect(_default, url, _curl=_curl)
        assert result.returncode in (7, 56), result.output
        assert f"http_code=000 http_connect={status}" in result.stdout


def verify_sni_denial(*, _curl: Curl, _sni: ATS) -> None:
    """Verify outbound policy also applies to SNI tunnel routes.

    :param _curl: Test-local curl configured by the test.
    :param _sni: Test-local sni configured by the test.
    """

    result = _curl.run_for(
        _sni,
        (
            f"--silent --insecure --verbose --resolve 'sni-denied.example.com:{_sni.https_port}:127.0.0.1' "
            f"'https://sni-denied.example.com:{_sni.https_port}/'"),
    )
    assert result.returncode in (35, 52, 56), result.output
    diagnostics = _sni.diags_log.read_text(errors="replace")
    assert re.search(r"server '127\.0\.0\.1.*' prohibited by ip-allow policy", diagnostics)


def test_connect_destination_acl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Outbound ip_allow protects CONNECT and SNI tunnel destinations.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("CONNECT destination ACL coverage requires a TCP listener")
    _origin = configure_origin(services)
    _default = configure_ats(ats_factory, "default", _origin=_origin)
    _allowed = configure_ats(ats_factory, "allowed", allow_loopback=True, _origin=_origin)
    _sni = configure_sni_ats(ats_factory, _origin=_origin)

    _origin.start()
    _default.start()
    _allowed.start()
    _sni.start()
    verify_denied_targets(_curl=curl, _default=_default, _origin=_origin)
    verify_sni_denial(_curl=curl, _sni=_sni)

    result = connect(_allowed, f"https://127.0.0.1:{_origin.https_port}/", _curl=curl)
    assert result.returncode == 0, result.output
    assert "http_code=200 http_connect=200" in result.stdout
