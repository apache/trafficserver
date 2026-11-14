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
"""Tests which client raw public keys an inbound connection trusts, per host and globally."""

import re
from collections.abc import Sequence
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, OriginServer, ServiceFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"

# The client authenticates with the raw public key of server.key, which server.pubkey.pem holds.
CLIENT_CERT = "server.pem"
CLIENT_KEY = "server.key"
# An X.509 client certificate that signer.pem, the global client CA, signed.
X509_CLIENT_CERT = "signed-foo.pem"
X509_CLIENT_KEY = "signed-foo.key"
TRUSTS_CLIENT = "server.pubkey.pem"
DISTRUSTS_CLIENT = "server.wrongpubkey.pem"


@pytest.fixture(autouse=True)
def require_rpk(ats_factory: ATSFactory) -> None:
    """Skip where raw public key support is not compiled in, which depends on the linked TLS library.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    if not ats_factory.has_feature("TS_USE_RPK"):
        pytest.skip("ATS was built without raw public key support")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create an origin whose response names the host it was requested for.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    for host in ("foo", "bar"):
        origin.add_response(
            {"headers": f"GET /{host}/ HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": f"{host} content"},
        )
    return origin


def configure_ats(
    ats_factory: ATSFactory,
    origin: OriginServer,
    *,
    sni_entries: Sequence[dict[str, str]] = (),
    global_rpk_file: str | None = None,
    cert_level: int = 0,
) -> ATS:
    """Configure foo.com and bar.com, each with its own certificate, in front of one origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param origin: Origin both hosts are served from.
    :param sni_entries: sni.yaml entries, each an fqdn plus any of verify_client_rpk_ca and
        verify_client_ca_certs, and each given verify_client STRICT. verify_client_rpk_ca is written
        bare and resolves against proxy.config.ssl.CA.cert.path; verify_client_ca_certs is written as a
        full path.
    :param global_rpk_file: proxy.config.ssl.CA.rpk.filename, or None to leave it unset.
    :param cert_level: proxy.config.ssl.client.certification_level.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.copy_to_ssl(
        SSL_DIRECTORY / "server.pem",
        SSL_DIRECTORY / "server.key",
        SSL_DIRECTORY / "server-foo.pem",
        SSL_DIRECTORY / "server-foo.key",
        SSL_DIRECTORY / "server-bar.pem",
        SSL_DIRECTORY / "server-bar.key",
        SSL_DIRECTORY / TRUSTS_CLIENT,
        SSL_DIRECTORY / DISTRUSTS_CLIENT,
        SSL_DIRECTORY / "signer.pem",
        SSL_DIRECTORY / X509_CLIENT_CERT,
        SSL_DIRECTORY / X509_CLIENT_KEY,
    )
    ats.set_ssl_multicert_yaml(
        {
            "ssl_multicert": [
                {"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"},
                {"ssl_cert_name": "server-foo.pem", "ssl_key_name": "server-foo.key"},
                {"ssl_cert_name": "server-bar.pem", "ssl_key_name": "server-bar.key"},
            ]
        }
    )
    if sni_entries:
        lines = ["sni:"]
        for entry in sni_entries:
            lines += [f"- fqdn: {entry['fqdn']}", "  verify_client: STRICT"]
            if "verify_client_rpk_ca" in entry:
                # Deliberately bare, so it has to resolve against proxy.config.ssl.CA.cert.path.
                lines.append(f"  verify_client_rpk_ca: {entry['verify_client_rpk_ca']}")
            if "verify_client_ca_certs" in entry:
                # Relative to the configuration directory rather than the CA path, so given in full.
                lines.append(f"  verify_client_ca_certs: {ats.ssl_directory / entry['verify_client_ca_certs']}")
        ats.write_config_file("sni.yaml", "\n".join(lines) + "\n")
    ats.remap_config.add_lines(
        [
            f"map https://foo.com/ http://127.0.0.1:{origin.port}/foo/",
            f"map https://bar.com/ http://127.0.0.1:{origin.port}/bar/",
        ]
    )
    records: dict[str, object] = {
        "proxy.config.ssl.CA.cert.path": str(ats.ssl_directory),
        # Does not sign server.pem, the certificate presented alongside the client's raw public key, so
        # only that key can authenticate it.
        "proxy.config.ssl.CA.cert.filename": "signer.pem",
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl_verify|ssl_sni",
    }
    if global_rpk_file is not None:
        records["proxy.config.ssl.CA.rpk.filename"] = global_rpk_file
    if cert_level:
        records["proxy.config.ssl.client.certification_level"] = cert_level
    ats.records.update(records)
    return ats


def start(origin: OriginServer, ats: ATS) -> None:
    """Start the origin and ATS, skipping if openssl s_client cannot present a raw public key.

    :param origin: Origin behind ATS.
    :param ats: Traffic Server under test.
    """

    origin.start()
    ats.start()
    probe = ats.run_shell("openssl s_client -help 2>&1 | grep -q -- -enable_client_rpk")
    if probe.returncode != 0:
        pytest.skip("openssl s_client cannot authenticate with a raw public key")


def request(ats: ATS, sni: str, host: str, *, x509: bool = False) -> CommandResult:
    """Send one HTTP/1.1 request, authenticating with the client raw public key.

    The exit status is not checked: a refused handshake is one of the outcomes under test.

    :param ats: Traffic Server receiving the request.
    :param sni: Server name sent in the TLS handshake.
    :param host: Host header of the request.
    :param x509: Authenticate with the X.509 client certificate that signer.pem signed, instead.
    """

    ssl = ats.ssl_directory
    credential = (
        f"-cert {ssl / X509_CLIENT_CERT} -key {ssl / X509_CLIENT_KEY}"
        if x509
        else f"-cert {ssl / CLIENT_CERT} -key {ssl / CLIENT_KEY} -enable_client_rpk"
    )
    return ats.run_shell(
        f"printf 'GET / HTTP/1.1\\r\\nHost: {host}\\r\\nConnection: close\\r\\n\\r\\n' | "
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} -servername {sni} {credential} -ign_eof"
    )


def assert_served(result: CommandResult, host: str) -> None:
    """Require a request to have been served the content of one host.

    :param result: The request's s_client output.
    :param host: "foo" or "bar".
    """

    assert f"{host} content" in result.output, f"{host}.com should be served\n{result.output}"


def assert_refused_handshake(result: CommandResult) -> None:
    """Require a request's handshake to have been refused, so no HTTP response came back.

    :param result: The request's s_client output.
    """

    assert not re.search(r"HTTP/1\.1 \d{3}", result.output), f"the handshake should have been refused\n{result.output}"


def test_rpk_client_cannot_reach_host_that_does_not_trust_its_key(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A request for bar.com is refused on a connection authenticated with a key only foo.com trusts.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(
        ats_factory,
        origin,
        sni_entries=(
            {"fqdn": "foo.com", "verify_client_rpk_ca": TRUSTS_CLIENT},
            {"fqdn": "bar.com", "verify_client_rpk_ca": DISTRUSTS_CLIENT},
        ),
    )
    start(origin, ats)

    assert_served(request(ats, "foo.com", "foo.com"), "foo")
    assert_refused_handshake(request(ats, "bar.com", "bar.com"))
    cross_host = request(ats, "foo.com", "bar.com")
    ats.stop()

    content = ats.traffic_out.read_text(errors="replace")
    assert "pin match=yes" in content and "pin match=no" in content, (
        f"the client should have authenticated with its raw public key\n{content}"
    )
    assert "bar content" not in cross_host.output, (
        f"bar.com content was served to a client whose key only foo.com trusts\n{cross_host.output}"
    )
    assert "HTTP/1.1 403" in cross_host.output, f"host_sni_policy should refuse the request\n{cross_host.output}"


@pytest.mark.parametrize("global_rpk_file,served", ((TRUSTS_CLIENT, True), (DISTRUSTS_CLIENT, False)), ids=("match", "mismatch"))
def test_rpk_client_global_trust(ats_factory: ATSFactory, services: ServiceFactory, global_rpk_file: str, served: bool) -> None:
    """With no sni.yaml, the client's raw public key is pinned against the records.yaml set.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param global_rpk_file: proxy.config.ssl.CA.rpk.filename.
    :param served: Whether that set trusts the client's key.
    """

    origin = configure_origin(services)
    ats = configure_ats(ats_factory, origin, global_rpk_file=global_rpk_file, cert_level=2)
    start(origin, ats)

    result = request(ats, "foo.com", "foo.com")
    ats.stop()

    if served:
        assert_served(result, "foo")
    else:
        assert_refused_handshake(result)
    assert f"pin match={'yes' if served else 'no'}" in ats.traffic_out.read_text(errors="replace"), (
        "the client should have authenticated with its raw public key"
    )


def test_rpk_client_sni_trust_replaces_global(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A host's verify_client_rpk_ca replaces the records.yaml set for that host only.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(
        ats_factory,
        origin,
        sni_entries=({"fqdn": "bar.com", "verify_client_rpk_ca": DISTRUSTS_CLIENT},),
        global_rpk_file=TRUSTS_CLIENT,
        cert_level=2,
    )
    start(origin, ats)

    assert_refused_handshake(request(ats, "bar.com", "bar.com"))
    assert_served(request(ats, "foo.com", "foo.com"), "foo")
    ats.stop()

    content = ats.traffic_out.read_text(errors="replace")
    assert "pin match=no" in content and "pin match=yes" in content, (
        f"bar.com should have refused the key against its own set, and foo.com accepted it by the global one\n{content}"
    )


def test_rpk_client_verify_client_falls_back_to_global_trust(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A verify_client entry without verify_client_rpk_ca pins against the records.yaml set.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(ats_factory, origin, sni_entries=({"fqdn": "foo.com"},), global_rpk_file=TRUSTS_CLIENT)
    start(origin, ats)

    result = request(ats, "foo.com", "foo.com")
    ats.stop()

    assert_served(result, "foo")
    assert "pin match=yes" in ats.traffic_out.read_text(errors="replace"), (
        "the client should have authenticated with its raw public key"
    )


def test_rpk_client_ca_override_refuses_global_rpk(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A host that names only its client CA accepts no raw public key, even one the global set trusts.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(
        ats_factory,
        origin,
        sni_entries=({"fqdn": "foo.com", "verify_client_ca_certs": "signer.pem"},),
        global_rpk_file=TRUSTS_CLIENT,
    )
    start(origin, ats)

    # The raw public key is not negotiated, so the client falls back to its self-signed certificate.
    assert_refused_handshake(request(ats, "foo.com", "foo.com"))
    assert_served(request(ats, "foo.com", "foo.com", x509=True), "foo")
    ats.stop()

    assert "Client authenticated with a raw public key" not in ats.traffic_out.read_text(errors="replace"), (
        "foo.com must not negotiate a raw public key"
    )


def test_rpk_client_rpk_override_refuses_global_ca(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A host that names only its client raw public keys accepts no certificate, even one the global CA signed.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    origin = configure_origin(services)
    ats = configure_ats(
        ats_factory, origin, sni_entries=({"fqdn": "foo.com", "verify_client_rpk_ca": TRUSTS_CLIENT},), cert_level=2
    )
    start(origin, ats)

    assert_refused_handshake(request(ats, "foo.com", "foo.com", x509=True))
    assert_served(request(ats, "foo.com", "foo.com"), "foo")
    # bar.com has no trust of its own, so the global CA still authenticates the same certificate there.
    assert_served(request(ats, "bar.com", "bar.com", x509=True), "bar")
    ats.stop()

    assert "client certificate refused: foo.com trusts only raw public keys" in ats.traffic_out.read_text(errors="replace"), (
        "the certificate must be refused because foo.com trusts only raw public keys"
    )
