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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_origin(
    services: ServiceFactory,
    name: str,
    certificate_name: str,
    *,
    hosts: tuple[str, ...] | None = None,
) -> OriginServer:
    """Create one HTTPS origin using a signed certificate.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param certificate_name: Certificate name used by this test step.
    :param hosts: Hosts used by this test step.
    """

    origin = services.origin(
        name,
        ssl=True,
        clientkey=SSL_DIRECTORY / f"signed-{certificate_name}.key",
        clientcert=SSL_DIRECTORY / f"signed-{certificate_name}.pem",
    )
    response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"}
    for host in hosts or (f"{certificate_name}.com", f"bad{certificate_name}.com"):
        origin.add_response({"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"}, response)
    return origin


def configure_ats(
        ats_factory: ATSFactory, *, _bar: OriginServer, _default: OriginServer, _foo: OriginServer, _wild: OriginServer) -> ATS:
    """Configure global signature checks and stricter SNI policies.

    :param _bar: Test-local bar configured by the test.
    :param _default: Test-local default configured by the test.
    :param _foo: Test-local foo configured by the test.
    :param _wild: Test-local wild configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        SSL_DIRECTORY / "signed-foo.pem",
        SSL_DIRECTORY / "signed-foo.key",
        SSL_DIRECTORY / "signed-bar.pem",
        SSL_DIRECTORY / "signed-bar.key",
        SSL_DIRECTORY / "signed-wild.pem",
        SSL_DIRECTORY / "signed-wild.key",
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "signer.pem",
        SSL_DIRECTORY / "signer.key",
    )
    ats.remap_config.add_lines(
        (
            f"map / https://127.0.0.1:{_default.https_port}",
            f"map https://foo.com/ https://127.0.0.1:{_foo.https_port}",
            f"map https://bad_foo.com/ https://127.0.0.1:{_foo.https_port}",
            f"map https://bar.com/ https://127.0.0.1:{_bar.https_port}",
            f"map https://bad_bar.com/ https://127.0.0.1:{_bar.https_port}",
            f"map https://foo.wild.com/ https://127.0.0.1:{_wild.https_port}",
            f"map https://foo_bar.wild.com/ https://127.0.0.1:{_wild.https_port}",
        ))
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.ssl.client.verify.server.properties": "SIGNATURE",
            "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.url_remap.pristine_host_hdr": 1,
        })
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: bar.com\n"
        "    verify_server_policy: ENFORCED\n"
        "    verify_server_properties: ALL\n"
        '  - fqdn: "*.wild.com"\n'
        "    verify_server_policy: ENFORCED\n"
        "    verify_server_properties: ALL\n"
        "  - fqdn: bad_bar.com\n"
        "    verify_server_policy: ENFORCED\n"
        "    verify_server_properties: ALL\n",
    )
    return ats


def request(host: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request @a host through the ATS TLS listener.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    """

    result = _curl.run_for(
        _ats,
        f"--insecure --header 'Host: {host}' 'https://127.0.0.1:{_ats.https_port}/'",
    )
    assert result.returncode == 0, result.output
    return result.stdout


def test_tls_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SNI policies can tighten the global outbound certificate checks.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _foo = configure_origin(services, "server_foo", "foo")
    _bar = configure_origin(services, "server_bar", "bar")
    _wild = configure_origin(services, "server_wild", "wild", hosts=("bar.com",))
    _default = services.origin("server", ssl=True)
    _ats = configure_ats(ats_factory, _bar=_bar, _default=_default, _foo=_foo, _wild=_wild)

    _foo.start()
    _bar.start()
    _wild.start()
    _default.start()
    _ats.start()
    for host in ("foo.com", "bar.com", "foo.wild.com", "foo_bar.wild.com"):
        assert "Could Not Connect" not in request(host, _ats=_ats, _curl=curl)
    assert "Could Not Connect" in request("bad_bar.com", _ats=_ats, _curl=curl)

    diagnostics = _ats.diags_log.read_text(errors="replace")
    assert "verification failed" not in diagnostics
    assert "WARNING: SNI (bad_bar.com) not in certificate" in diagnostics
