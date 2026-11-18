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
"""Each certificate of an RSA+EC line is served with its own ssl_ca_name chain and OCSP response."""

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"
OCSP_CA = SSL_DIRECTORY / "ca.ocsp.pem"
EC_SERIAL = "0EC0CE5A"

# s_client options that restrict a client to one key type. TLS 1.2 also needs a matching cipher,
# since there the cipher suite names the certificate's key type.
CLIENT_OPTIONS = {
    "ecdsa": {
        "tls1_2": "-sigalgs ecdsa_secp256r1_sha256 -cipher ECDHE-ECDSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs ecdsa_secp256r1_sha256",
        "quic": "-sigalgs ecdsa_secp256r1_sha256",
    },
    "rsa": {
        "tls1_2": "-sigalgs rsa_pss_rsae_sha256 -cipher ECDHE-RSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs rsa_pss_rsae_sha256",
        "quic": "-sigalgs rsa_pss_rsae_sha256",
    },
}


@dataclass
class LineCertificate:
    """A certificate of the line with the entries configured for it."""

    certificate: Path
    """PEM certificate, with its key beside it in a .key file."""

    serial: str
    """Certificate serial number as OpenSSL prints it."""

    chain: Path
    """The certificate's ssl_ca_name file: an unrelated CA certificate that marks which chain a client receives."""

    ocsp_response: Path
    """The certificate's prefetched OCSP response."""


def run_openssl(*arguments: str | Path) -> None:
    """Run an openssl command, failing the test if it fails.

    :param arguments: Arguments after "openssl".
    """

    subprocess.run(["openssl", *map(str, arguments)], check=True, capture_output=True)


def generate_ec_certificate(directory: Path) -> LineCertificate:
    """Generate an EC certificate issued by the OCSP test CA, and a good OCSP response for it.

    :param directory: Directory that receives the generated files.
    """

    directory.mkdir(parents=True, exist_ok=True)
    certificate = directory / "server.ocsp-ec.pem"
    key = certificate.with_suffix(".key")
    request = directory / "server.ocsp-ec.csr"
    extensions = directory / "server.ocsp-ec.ext"
    extensions.write_text(
        "basicConstraints=CA:FALSE\nsubjectAltName=DNS:server.example.com\nauthorityInfoAccess=OCSP;URI:http://localhost:8888\n"
    )
    run_openssl(
        "req",
        "-new",
        "-newkey",
        "ec",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-nodes",
        "-keyout",
        key,
        "-subj",
        "/CN=server.example.com",
        "-out",
        request,
    )
    run_openssl(
        "x509",
        "-req",
        "-in",
        request,
        "-CA",
        OCSP_CA,
        "-CAkey",
        SSL_DIRECTORY / "ca.ocsp.key",
        "-set_serial",
        f"0x{EC_SERIAL}",
        "-days",
        "1",
        "-extfile",
        extensions,
        "-out",
        certificate,
    )

    index = directory / "ocsp-index.txt"
    index.write_text(f"V\t491231235959Z\t\t{EC_SERIAL}\tunknown\t/CN=server.example.com\n")
    ocsp_request = directory / "server.ocsp-ec.req.der"
    response = directory / "ocsp_response-ec.der"
    run_openssl("ocsp", "-issuer", OCSP_CA, "-cert", certificate, "-no_nonce", "-reqout", ocsp_request)
    run_openssl(
        "ocsp",
        "-index",
        index,
        "-CA",
        OCSP_CA,
        "-rsigner",
        SSL_DIRECTORY / "responder.ocsp.pem",
        "-rkey",
        SSL_DIRECTORY / "responder.ocsp.key",
        "-reqin",
        ocsp_request,
        "-respout",
        response,
        "-ndays",
        "1",
    )
    return LineCertificate(certificate, EC_SERIAL, SSL_DIRECTORY / "bbb-ca.pem", response)


def start_ats(ats_factory: ATSFactory, version: str) -> tuple[ATS, dict[str, LineCertificate]]:
    """Start Traffic Server with one line holding an RSA and an EC certificate, the RSA one first.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: Transport key within each CLIENT_OPTIONS entry, for the transport the clients will use; "quic" also
        opens the QUIC listener.
    :return: The running Traffic Server, and the line's certificates keyed by the client in CLIENT_OPTIONS that
        must receive each.
    """

    if version == "quic":
        if not ats_factory.has_feature("TS_USE_QUIC"):
            pytest.skip("ATS was built without QUIC")
        openssl_version = re.search(r"\d+(?:\.\d+)+", subprocess.check_output(("openssl", "version"), text=True))
        if openssl_version is None or tuple(int(part) for part in openssl_version.group().split(".")) < (3, 5, 0):
            pytest.skip("s_client needs OpenSSL 3.5 or later for QUIC")

    certificates = {
        "rsa": LineCertificate(
            SSL_DIRECTORY / "server.ocsp.pem",
            "672048354C2FF0CF7C46BDF507DCB6CD7CAE1430",
            SSL_DIRECTORY / "aaa-ca.pem",
            SSL_DIRECTORY / "ocsp_response.der",
        ),
        "ecdsa": generate_ec_certificate(ats_factory.run_directory),
    }
    line = (certificates["rsa"], certificates["ecdsa"])

    ats = ats_factory.create("ts", enable_tls=True, enable_quic=version == "quic")
    ats.copy_to_ssl(OCSP_CA)
    for entry in line:
        ats.copy_to_ssl(entry.certificate, entry.certificate.with_suffix(".key"), entry.chain, entry.ocsp_response)
    ats.set_ssl_multicert_yaml(
        {
            "ssl_multicert": [
                {
                    "dest_ip": "*",
                    "ssl_cert_name": ",".join(entry.certificate.name for entry in line),
                    "ssl_key_name": ",".join(entry.certificate.with_suffix(".key").name for entry in line),
                    "ssl_ca_name": ",".join(entry.chain.name for entry in line),
                    "ssl_ocsp_name": ",".join(entry.ocsp_response.name for entry in line),
                }
            ]
        }
    )
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.cert_chain.filename": OCSP_CA.name,
            "proxy.config.ssl.server.cipher_suite": "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256",
            "proxy.config.ssl.ocsp.enabled": 1,
            "proxy.config.ssl.ocsp.response.path": str(ats.ssl_directory),
            "proxy.config.quic.server.stateless_retry_enabled": 0,
        }
    )
    ats.start()
    return ats, certificates


def certificate_body(certificate: Path) -> str:
    """Return the first PEM certificate in @a certificate up to its END line.

    :param certificate: PEM certificate file.
    """

    content = certificate.read_text()
    return content[content.index("-----BEGIN CERTIFICATE-----") : content.index("-----END CERTIFICATE-----")]


def handshake(ats: ATS, version: str, client: str) -> str:
    """Complete a handshake as @a client, requesting OCSP stapling, and return the s_client output.

    :param ats: The running Traffic Server.
    :param version: s_client protocol option without the leading dash, or "quic" for TLS 1.3 over QUIC.
    :param client: Key in CLIENT_OPTIONS naming the key type the client accepts.
    """

    transport = "-quic -alpn h3" if version == "quic" else f"-{version}"
    result = ats.run_shell(
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} {transport} -servername server.example.com -status -showcerts "
        f"{CLIENT_OPTIONS[client][version]} </dev/null"
    )
    assert result.returncode == 0, f"{version} {client} client: {result.output}"
    # An s_client built without QUIC still accepts -quic and connects over TCP, where the TLS listener shares the port.
    if version == "quic":
        assert "Protocol: QUICv1" in result.output, f"{client} client did not use QUIC: {result.output}"
    return result.output


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3", "quic"))
def test_each_certificate_gets_its_own_chain(ats_factory: ATSFactory, version: str) -> None:
    """Each client receives the ssl_ca_name chain of the certificate it gets, not the other certificate's.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: Transport key within each CLIENT_OPTIONS entry, for the transport the clients use.
    """

    ats, certificates = start_ats(ats_factory, version)
    for client, expected in certificates.items():
        other = next(entry for name, entry in certificates.items() if name != client)
        output = handshake(ats, version, client)
        label = f"{version} {client} client"
        assert certificate_body(expected.certificate) in output, f"{label} did not receive {expected.certificate.name}: {output}"
        assert certificate_body(expected.chain) in output, f"{label} did not receive {expected.chain.name}: {output}"
        assert certificate_body(other.chain) not in output, f"{label} received {other.chain.name}: {output}"


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3", "quic"))
def test_each_certificate_gets_its_own_ocsp_response(ats_factory: ATSFactory, version: str) -> None:
    """Each client receives the stapled OCSP response of the certificate it gets, not the other certificate's.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: Transport key within each CLIENT_OPTIONS entry, for the transport the clients use.
    """

    ats, certificates = start_ats(ats_factory, version)
    for client, expected in certificates.items():
        other = next(entry for name, entry in certificates.items() if name != client)
        output = handshake(ats, version, client)
        label = f"{version} {client} client"
        assert certificate_body(expected.certificate) in output, f"{label} did not receive {expected.certificate.name}: {output}"
        assert "OCSP Response Status: successful" in output, f"{label} received no OCSP response: {output}"
        assert re.search(rf"^\s*Serial Number: {expected.serial}$", output, re.MULTILINE), (
            f"{label} did not receive the OCSP response for {expected.certificate.name}: {output}"
        )
        assert not re.search(rf"^\s*Serial Number: {other.serial}$", output, re.MULTILINE), (
            f"{label} received the OCSP response for {other.certificate.name}: {output}"
        )
