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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve all origin names to the loopback services.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({
        "foo.com.": ["127.0.0.1"],
        "bar.com.": ["127.0.0.1"],
        "random.com.": ["127.0.0.1"],
    })
    return dns


def configure_origin(services: ServiceFactory, name: str, host: str) -> OriginServer:
    """Create a mutually authenticated origin using the foo certificate.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param host: HTTP host name used for the request.
    """

    origin = services.origin(
        name,
        ssl=True,
        clientkey=SSL_DIRECTORY / "signed-foo.key",
        clientcert=SSL_DIRECTORY / "signed-foo.pem",
        options={
            "--clientCA": SSL_DIRECTORY / "signer.pem",
            "--clientverify": "",
        },
    )
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _bar: OriginServer, _dns: DNSServer, _foo: OriginServer) -> ATS:
    """Configure permissive defaults and per-remap verification overrides.

    :param _bar: Test-local bar configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _foo: Test-local foo configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
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
    ats.remap_config.add_lines(
        (
            f"map http://foo.com/defaultbar https://bar.com:{_bar.https_port}",
            f"map http://foo.com/default https://foo.com:{_foo.https_port}",
            f"map http://foo.com/overridepolicy https://bar.com:{_foo.https_port} "
            "@plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED",
            f"map http://foo.com/overrideproperties https://bar.com:{_foo.https_port} "
            "@plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=SIGNATURE",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.ssl.client.verify.server.properties": "ALL",
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.client.sni_policy": "remap",
        })
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: bar.com\n"
        f'    client_cert: "{ats.ssl_directory}/signed-foo.pem"\n'
        f'    client_key: "{ats.ssl_directory}/signed-foo.key"\n',
    )
    return ats


def request(path: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request one remap case through the clear-text ATS listener.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    """

    result = _curl.run_for(
        _ats,
        f"--header 'Host: foo.com' 'http://127.0.0.1:{_ats.http_port}/{path}'",
    )
    assert result.returncode == 0, result.output
    return result.stdout


def test_tls_verify_override_sni(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """conf_remap can change outbound server verification per mapping.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _dns = configure_dns(services)
    _foo = configure_origin(services, "server_foo", "foo.com")
    _bar = configure_origin(services, "server_bar", "bar.com")
    _ats = configure_ats(ats_factory, _bar=_bar, _dns=_dns, _foo=_foo)

    _dns.start()
    _foo.start()
    _bar.start()
    _ats.start()
    assert "Could Not Connect" not in request("defaultbar", _ats=_ats, _curl=curl)
    assert "Could Not Connect" in request("overridepolicy", _ats=_ats, _curl=curl)
    assert "Could Not Connect" not in request("overrideproperties", _ats=_ats, _curl=curl)

    diagnostics = _ats.diags_log.read_text(errors="replace")
    assert "WARNING: SNI (bar.com) not in certificate. Action=Continue server=bar.com" in diagnostics
    assert "WARNING: SNI (bar.com) not in certificate. Action=Terminate server=bar.com" in diagnostics
