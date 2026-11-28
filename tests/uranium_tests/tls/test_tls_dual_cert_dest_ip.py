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
"""Certificate selection by destination address follows the key type the client can use."""

import subprocess
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"
FOO_RSA = SSL_DIRECTORY / "signed-foo.pem"
FOO_EC = SSL_DIRECTORY / "signed-foo-ec.pem"

# s_client options that restrict a client to one key type. TLS 1.2 also needs a matching cipher,
# since there the cipher suite names the certificate's key type.
CLIENT_OPTIONS = {
    "ecdsa": {
        "tls1_2": "-sigalgs ecdsa_secp256r1_sha256 -cipher ECDHE-ECDSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs ecdsa_secp256r1_sha256",
    },
    "ecdsa_p384": {
        "tls1_2": "-sigalgs ecdsa_secp384r1_sha384 -groups P-384 -cipher ECDHE-ECDSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs ecdsa_secp384r1_sha384",
    },
    "rsa": {
        "tls1_2": "-sigalgs rsa_pss_rsae_sha256 -cipher ECDHE-RSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs rsa_pss_rsae_sha256",
    },
    "any": {"tls1_2": "", "tls1_3": ""},
}


def generate_p384_certificate(ats_factory: ATSFactory) -> Path:
    """Generate a self-signed P-384 certificate, with its key beside it, in the test's sandbox.

    :param ats_factory: Factory whose sandbox holds the generated files.
    """

    certificate = ats_factory.run_directory / "p384.pem"
    certificate.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "ec",
            "-pkeyopt",
            "ec_paramgen_curve:P-384",
            "-nodes",
            "-keyout",
            str(certificate.with_suffix(".key")),
            "-out",
            str(certificate),
            "-subj",
            "/CN=p384.example.com",
            "-days",
            "1",
        ],
        check=True,
        capture_output=True,
    )
    return certificate


def start_ats(ats_factory: ATSFactory, entries: list[tuple[str, tuple[Path, ...]]]) -> ATS:
    """Start Traffic Server with @a entries, followed by an RSA+EC pair with other names for every other address.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param entries: dest_ip values, where "{port}" stands for the TLS port, each with its PEM certificates; every
        certificate has its key beside it in a .key file.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    catch_all = (SSL_DIRECTORY / "signed-san-ec.pem", SSL_DIRECTORY / "signed-san.pem")
    lines = [*((dest_ip.format(port=ats.https_port), certificates) for dest_ip, certificates in entries), ("*", catch_all)]
    for certificate in {certificate for _, certificates in lines for certificate in certificates}:
        ats.copy_to_ssl(certificate, certificate.with_suffix(".key"))
    ats.set_ssl_multicert_yaml(
        {
            "ssl_multicert": [
                {
                    "dest_ip": dest_ip,
                    "ssl_cert_name": ",".join(certificate.name for certificate in certificates),
                    "ssl_key_name": ",".join(certificate.with_suffix(".key").name for certificate in certificates),
                }
                for dest_ip, certificates in lines
            ]
        }
    )
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.cipher_suite": "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256",
        }
    )
    ats.start()
    return ats


def certificate_prefix(certificate: Path) -> str:
    """Return the PEM certificate in @a certificate up to its end marker.

    :param certificate: PEM certificate file.
    """

    content = certificate.read_text()
    return content[: content.index("END CERTIFICATE-----")]


def assert_served(ats: ATS, version: str, client: str, certificate: Path) -> None:
    """Require that a client without SNI completes a handshake and receives @a certificate.

    :param ats: The running Traffic Server.
    :param version: s_client protocol option, without the leading dash.
    :param client: Key in CLIENT_OPTIONS naming the key types the client accepts.
    :param certificate: Certificate file the client must receive.
    """

    result = ats.run_shell(
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} -{version} -noservername "
        f"{CLIENT_OPTIONS[client][version]} </dev/null"
    )
    label = f"{version} {client} client"
    assert result.returncode == 0, f"{label}: {result.output}"
    assert certificate_prefix(certificate) in result.output, f"{label} did not receive {certificate.name}: {result.output}"


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_dual_address_line_serves_each_key_type(ats_factory: ATSFactory, version: str) -> None:
    """Clients that can use only one key type each get that certificate from an RSA+EC address line.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    ats = start_ats(ats_factory, [("127.0.0.1", (FOO_EC, FOO_RSA))])
    assert_served(ats, version, "ecdsa", FOO_EC)
    assert_served(ats, version, "rsa", FOO_RSA)


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_port_line_serves_rsa_client(ats_factory: ATSFactory, version: str) -> None:
    """An RSA-only client gets the RSA certificate of an address and port line, not the EC certificate
    of a line for the address alone.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    ats = start_ats(ats_factory, [("127.0.0.1:{port}", (FOO_RSA,)), ("127.0.0.1", (FOO_EC,))])
    assert_served(ats, version, "rsa", FOO_RSA)


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_rsa_address_line_serves_ec_capable_client(ats_factory: ATSFactory, version: str) -> None:
    """A client that can use either key type gets the RSA certificate of an RSA-only address line,
    not the EC certificate of the catch-all line. This also holds without key-type-aware lookup; it
    guards against a lookup that leaves the address when it has no certificate of the client's type.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    ats = start_ats(ats_factory, [("127.0.0.1", (FOO_RSA,))])
    assert_served(ats, version, "any", FOO_RSA)
    assert_served(ats, version, "rsa", FOO_RSA)


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_ec_address_line_serves_p384_client(ats_factory: ATSFactory, version: str) -> None:
    """A client that supports only P-384 gets the P-384 certificate of an EC-only address line, not
    the catch-all line, although on BoringSSL builds Traffic Server does not recognize it as able to
    use EC. This also holds without key-type-aware lookup; it guards against a lookup that leaves the
    address.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    p384 = generate_p384_certificate(ats_factory)
    ats = start_ats(ats_factory, [("127.0.0.1", (p384,))])
    assert_served(ats, version, "ecdsa_p384", p384)


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_ec_port_line_serves_p384_client(ats_factory: ATSFactory, version: str) -> None:
    """A client that supports only P-384 gets the P-384 certificate of an EC-only address and port
    line, not the RSA certificate of a line for the address alone. This also holds without
    key-type-aware lookup; it guards against a lookup that prefers the key type over the more
    specific line.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    p384 = generate_p384_certificate(ats_factory)
    ats = start_ats(ats_factory, [("127.0.0.1:{port}", (p384,)), ("127.0.0.1", (FOO_RSA,))])
    assert_served(ats, version, "ecdsa_p384", p384)
