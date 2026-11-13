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
import sys

from tools.uranium.services import ATS, ATSFactory, CommandResult, OriginServer, ServiceFactory, wait_for_file_lines

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin that answers every request that passes the client-certificate policy.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: origin\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, origin: OriginServer) -> ATS:
    """Require a client certificate for admin.test only, and force a HelloRetryRequest for clients that
    send their default key shares.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param origin: Origin behind every remapped request.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "signer.pem")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]})
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.client.certification_level": 0,
            "proxy.config.ssl.CA.cert.path": "",
            "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
            # Clients offer key shares for X25519-based groups by default, so P-256 alone makes the
            # server ask for another ClientHello.
            "proxy.config.ssl.server.groups_list": "P-256",
            "proxy.config.http.host_sni_policy": 2,
            "proxy.config.url_remap.pristine_host_hdr": 1,
        }
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    ats.write_config_file(
        "sni.yaml",
        "sni:\n  - fqdn: www.test\n    verify_client: NONE\n  - fqdn: admin.test\n    verify_client: STRICT\n",
    )
    return ats


def connect(
    services: ServiceFactory, ats: ATS, name: str, first: str, second: str, host: str, certificate: bool = False
) -> CommandResult:
    """Send one request whose ClientHello names @a first and whose post-HelloRetryRequest ClientHello names @a second.

    :param services: Factory owning the client process.
    :param ats: Traffic Server under test.
    :param name: Unique client process name.
    :param first: Server name in the first ClientHello.
    :param second: Server name in the second ClientHello.
    :param host: Host header of the request.
    :param certificate: Whether to offer a client certificate the signer CA issued.
    """

    arguments: list[str | Path] = [
        sys.executable,
        Path(__file__).with_name("tls_hrr_sni_client.py"),
        "--port",
        str(ats.https_port),
        "--sni1",
        first,
        "--sni2",
        second,
        "--host",
        host,
    ]
    if certificate:
        arguments += ["--cert", SSL_DIRECTORY / "signed-foo.pem", "--key", SSL_DIRECTORY / "signed-foo.key"]
    result = services.process(name, arguments).run(timeout=30)
    assert result.returncode == 0, result.output
    assert "hrr=True" in result.output, result.output
    return result


def test_tls_hrr_sni_change(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A ClientHello after a HelloRetryRequest cannot switch to a server name with a stricter client-certificate policy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(ats_factory, origin)
    origin.start()
    ats.start()

    result = connect(services, ats, "relaxed", "www.test", "www.test", "www.test")
    assert "certificate_request=False" in result.output, result.output
    assert "status=HTTP/1.1 200 OK" in result.output, result.output

    result = connect(services, ats, "strict-no-cert", "admin.test", "admin.test", "admin.test")
    assert "certificate_request=True" in result.output, result.output
    assert "status=HTTP/1.1 200" not in result.output, result.output

    result = connect(services, ats, "strict-cert", "admin.test", "admin.test", "admin.test", certificate=True)
    assert "certificate_request=True" in result.output, result.output
    assert "status=HTTP/1.1 200 OK" in result.output, result.output

    result = connect(services, ats, "switched", "www.test", "admin.test", "admin.test")
    assert "status=HTTP/1.1 200" not in result.output, result.output
    library = ats.tls_library()
    if "BoringSSL" in library:
        # BoringSSL negotiates from the first ClientHello alone, so the connection stays www.test and
        # the admin.test request is refused as a host and SNI mismatch.
        assert "handshake=ok" in result.output, result.output
        assert "status=HTTP/1.1 403" in result.output, result.output
    elif library.startswith("OpenSSL") and "compatible" not in library:
        # OpenSSL hands ATS the second ClientHello, which ATS rejects.
        assert "handshake=failed" in result.output, result.output
        assert "illegal parameter" in result.output, result.output
        wait_for_file_lines(
            ats.diags_log, "WARNING:.* rejecting a ClientHello after a HelloRetryRequest that changes the server name", 1
        )
