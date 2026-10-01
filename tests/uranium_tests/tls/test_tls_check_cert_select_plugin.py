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
import shutil
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the HTTPS origin reached after inbound TLS selection.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", ssl=True)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )
    return origin


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the outbound origin name.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({"foo.com.": ["127.0.0.1"], "bar.com.": ["127.0.0.1"]})
    return dns


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer, _origin: OriginServer) -> ATS:
    """Configure address, SNI, and fallback certificates through the hook.

    :param _dns: Test-local dns configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    names = (
        "signed-foo.pem",
        "signed-foo.key",
        "signed-bar.pem",
        "signed2-bar.pem",
        "signed-bar.key",
        "server.pem",
        "server.key",
        "signer.pem",
        "signer.key",
    )
    ats.copy_to_ssl(*(SSL_DIRECTORY / name for name in names))
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_secret_load_test.so")
    ats.plugin_config.add_line("ssl_secret_load_test.so")
    ats.remap_config.add_line(f"map / https://foo.com:{_origin.https_port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "127.0.0.1"',
            "    ssl_cert_name: signed-foo.pem",
            "    ssl_key_name: signed-foo.key",
            "  - ssl_cert_name: signed2-bar.pem",
            "    ssl_key_name: signed-bar.key",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.tags": "ssl_secret_load_test|ssl",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory.parent),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory.parent),
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    return ats


def request(hostname: str, ca_file: str | None = None, expected_code: int = 0, *, _ats: ATS, _curl: Curl) -> str:
    """Connect to one SNI name and return curl's TLS diagnostics.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    :param ca_file: Path to the ca file.
    :param expected_code: Expected code for this case.
    """

    arguments = ["--verbose"]
    if ca_file is None:
        arguments.append("--insecure")
    else:
        arguments.extend(("--cacert", str(SSL_DIRECTORY / ca_file)))
    arguments.extend((
        "--resolve",
        f"{hostname}:{_ats.https_port}:127.0.0.1",
        f"https://{hostname}:{_ats.https_port}",
    ))
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == expected_code, result.output
    return result.output


def refresh_bar_certificate(*, _ats: ATS) -> None:
    """Replace the watched bar certificate and await the plugin poll.

    :param _ats: Test-local ats configured by the test.
    """

    time.sleep(1.1)
    live = _ats.ssl_directory / "signed2-bar.pem"
    shutil.copyfile(SSL_DIRECTORY / "signed-bar.pem", live)
    live.touch()
    time.sleep(4)


def test_tls_check_cert_select_plugin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The TLS secret hook selects and refreshes inbound certificates.

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
    assert "CN=bar.com" in bar and "CN=foo.com" not in bar and re.search(r"HTTP/[\d.]+ 404", bar)
    foo = request("foo.com", "signer.pem", _ats=_ats, _curl=curl)
    assert "CN=foo.com" in foo and "CN=bar.com" not in foo and re.search(r"HTTP/[\d.]+ 404", foo)
    fallback = request("random.server.com", _ats=_ats, _curl=curl)
    assert "CN=random.server.com" in fallback and re.search(r"HTTP/[\d.]+ 404", fallback)
    bad_sni = request("bad.sni.com", _ats=_ats, _curl=curl)
    assert "CN=foo.com" in bad_sni and "CN=bar.com" not in bad_sni

    refresh_bar_certificate(_ats=_ats)
    refreshed = request("bar.com", "signer.pem", _ats=_ats, _curl=curl)
    assert "CN=bar.com" in refreshed and re.search(r"HTTP/[\d.]+ 404", refreshed)
    rejected = request("bar.com", "signer2.pem", 60, _ats=_ats, _curl=curl)
    assert "curl: (60) SSL certificate" in rejected
