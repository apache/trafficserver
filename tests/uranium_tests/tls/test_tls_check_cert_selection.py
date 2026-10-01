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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the HTTPS origin used after the inbound handshake.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the outbound foo.com origin.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({"foo.com": ["127.0.0.1"], "bar.com": ["127.0.0.1"]})
    return dns


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Install address-specific, SNI, and fallback certificates.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        *(
            TEST_DIRECTORY / "ssl" / name for name in (
                "signed-foo.pem",
                "signed-foo.key",
                "signed-bar.pem",
                "signed2-bar.pem",
                "signed-bar.key",
                "signer.pem",
                "signer.key",
                "combo.pem",
            )))
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "127.0.0.1"',
            "    ssl_cert_name: signed-foo.pem",
            "    ssl_key_name: signed-foo.key",
            "  - ssl_cert_name: signed2-bar.pem",
            "    ssl_key_name: signed-bar.key",
            '  - dest_ip: "*"',
            "    ssl_cert_name: combo.pem",
        ))
    ats.remap_config.add_line(f"map / https://foo.com:{_origin.https_port}")
    ats.records.update(
        {
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    return ats


def request(hostname: str, ca_file: str | None = None, *, _ats: ATS, _curl: Curl) -> str:
    """Connect with @a hostname and return curl's handshake diagnostics.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    :param ca_file: Path to the ca file.
    """

    arguments = ["--verbose"]
    if ca_file is None:
        arguments.append("--insecure")
    else:
        arguments.extend(("--cacert", str(TEST_DIRECTORY / "ssl" / ca_file)))
    arguments.extend((
        "--resolve",
        f"{hostname}:{_ats.https_port}:127.0.0.1",
        f"https://{hostname}:{_ats.https_port}",
    ))
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    assert "Could Not Connect" not in result.output
    return result.output


def test_tls_check_cert_selection(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS offers the correct certificate for SNI and destination address.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("SNI certificate selection requires a TCP listener")
    _origin = configure_origin(services)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _origin=_origin)

    _origin.start()
    _dns.start()
    _ats.start()
    bar = request("bar.com", "signer2.pem", _ats=_ats, _curl=curl)
    assert "CN=bar.com" in bar and "CN=foo.com" not in bar
    foo = request("foo.com", "signer.pem", _ats=_ats, _curl=curl)
    assert "CN=foo.com" in foo and "CN=bar.com" not in foo
    fallback = request("random.server.com", _ats=_ats, _curl=curl)
    assert "CN=random.server.com" in fallback
    assert "CN=foo.com" not in fallback and "CN=bar.com" not in fallback
    bad_sni = request("bad.sni.com", _ats=_ats, _curl=curl)
    assert "CN=foo.com" in bad_sni and "CN=bar.com" not in bad_sni
