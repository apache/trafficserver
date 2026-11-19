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
"""A certificate replaced through TSSslServerCertUpdate gets its own OCSP stapling, not the replaced certificate's."""

import re
import subprocess
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

SSL_DIRECTORY = Path(__file__).parent / "ssl"
ORIGINAL = SSL_DIRECTORY / "server.ocsp.pem"
OCSP_CA = SSL_DIRECTORY / "ca.ocsp.pem"
# Each client accepts only one key type's certificate.
CLIENT_OPTIONS = {
    "rsa": {"tls1_2": "-sigalgs rsa_pss_rsae_sha256", "tls1_3": "-sigalgs rsa_pss_rsae_sha256"},
    "ecdsa": {
        "tls1_2": "-sigalgs ecdsa_secp256r1_sha256 -cipher ECDHE-ECDSA-AES128-GCM-SHA256",
        "tls1_3": "-sigalgs ecdsa_secp256r1_sha256",
    },
}


def openssl(*arguments: str | Path) -> str:
    """Run openssl and return its standard output, failing the test with its standard error if it fails.

    :param arguments: Arguments after the openssl command name.
    """

    result = subprocess.run(["openssl", *map(str, arguments)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr
    return result.stdout


def serial_of(certificate: Path) -> str:
    """Return the serial number of @a certificate as OpenSSL prints it in an OCSP response.

    :param certificate: PEM certificate.
    """

    return openssl("x509", "-in", certificate, "-noout", "-serial").strip().partition("=")[2]


def issue(directory: Path, name: str, key_options: tuple[str, ...], serial: int) -> Path:
    """Issue a certificate for server.example.com from the OCSP test CA, naming a responder nothing answers at.

    :param directory: Directory that receives the certificate, its key and intermediate files.
    :param name: File name of the certificate without its extension; the key is beside it in a .key file.
    :param key_options: openssl req -newkey arguments for the key.
    :param serial: Serial number of the certificate.
    :return: The PEM certificate.
    """

    key = directory / f"{name}.key"
    request = directory / f"{name}.csr"
    extensions = directory / f"{name}.ext"
    certificate = directory / f"{name}.pem"
    extensions.write_text(
        "basicConstraints=CA:FALSE\nsubjectAltName=DNS:server.example.com\nauthorityInfoAccess=OCSP;URI:http://127.0.0.1:1\n"
    )
    openssl("req", "-new", "-newkey", *key_options, "-nodes", "-keyout", key, "-subj", "/CN=server.example.com", "-out", request)
    openssl(
        "x509",
        "-req",
        "-in",
        request,
        "-CA",
        OCSP_CA,
        "-CAkey",
        OCSP_CA.with_suffix(".key"),
        "-set_serial",
        str(serial),
        "-days",
        "1",
        "-extfile",
        extensions,
        "-out",
        certificate,
    )
    return certificate


def replacement_file(certificate: Path) -> Path:
    """Write @a certificate followed by its key into one file, which is how TSSslServerCertUpdate reads them.

    :param certificate: PEM certificate with its key beside it in a .key file.
    """

    replacement = certificate.with_name("replacement.pem")
    replacement.write_text(certificate.read_text() + certificate.with_suffix(".key").read_text())
    return replacement


def ocsp_response(certificate: Path, response: Path) -> Path:
    """Write a good OCSP response for @a certificate, signed by the OCSP test responder, to @a response.

    :param certificate: PEM certificate issued by OCSP_CA.
    :param response: Path of the DER response to write.
    """

    index = response.with_suffix(".index")
    index.write_text(f"V\t491231235959Z\t\t{serial_of(certificate)}\tunknown\t/CN=server.example.com\n")
    request = response.with_suffix(".request")
    openssl("ocsp", "-issuer", OCSP_CA, "-cert", certificate, "-no_nonce", "-reqout", request)
    openssl(
        "ocsp",
        "-index",
        index,
        "-CA",
        OCSP_CA,
        "-rsigner",
        SSL_DIRECTORY / "responder.ocsp.pem",
        "-rkey",
        SSL_DIRECTORY / "responder.ocsp.key",
        "-ndays",
        "1",
        "-reqin",
        request,
        "-respout",
        response,
    )
    return response


def start_ats(ats_factory: ATSFactory, certificates: list[Path], responses: list[Path]) -> ATS:
    """Start Traffic Server with one line holding @a certificates and their prefetched @a responses, and cert_update.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param certificates: PEM certificates of the line, each with its key beside it in a .key file.
    :param responses: DER OCSP response for each certificate, in the same order.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    if not ats.plugin_exists("cert_update.so"):
        pytest.skip("cert_update.so was not built")
    ats.copy_to_ssl(OCSP_CA, *certificates, *(certificate.with_suffix(".key") for certificate in certificates), *responses)
    ats.ssl_multicert_config.add_lines(
        [
            "ssl_multicert:",
            '  - dest_ip: "*"',
            f"    ssl_cert_name: {','.join(certificate.name for certificate in certificates)}",
            f"    ssl_key_name: {','.join(certificate.with_suffix('.key').name for certificate in certificates)}",
            f"    ssl_ocsp_name: {','.join(response.name for response in responses)}",
        ]
    )
    ats.records.update(
        {
            "proxy.config.ssl.server.cert_chain.filename": OCSP_CA.name,
            "proxy.config.ssl.ocsp.enabled": 1,
            "proxy.config.ssl.ocsp.response.path": str(ats.ssl_directory),
            # The prefetched responses stay fresh for the whole test.
            "proxy.config.ssl.ocsp.cache_timeout": 3600,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "cert_update",
        }
    )
    ats.plugin_config.add_line("cert_update.so")
    # The new certificate has no response yet, and nothing answers at its responder, so the refresh that runs after
    # startup fails to fetch one.
    ats.allow_errors(r"ssl_callback_ocsp_stapling: failed to get certificate status for .*replacement\.pem")
    ats.allow_errors(r"(failed to (get|parse) a response from OCSP server|OCSP request was timed out); uri=http://127\.0\.0\.1:1")
    ats.allow_errors(r"stapling_refresh_response: failed to refresh OCSP response")
    ats.allow_errors(r"Failed to refresh OCSP for .*replacement\.pem certificate")
    ats.start()
    return ats


def update_certificate(ats: ATS, replacement: Path) -> None:
    """Replace the line's certificate of @a replacement's key type through cert_update, and wait for its stapling setup.

    :param ats: The running Traffic Server.
    :param replacement: File holding the new certificate followed by its key.
    """

    result = ats.traffic_ctl("plugin", "msg", "cert_update.server", str(replacement))
    assert result.returncode == 0, result.output
    wait_for_file_lines(ats.traffic_out, "Successfully updated server cert", 1)
    wait_for_file_lines(ats.diags_log, re.escape(f"successfully initialized stapling for {replacement}"), 1)


def stapled_serial(ats: ATS, version: str, client: str) -> tuple[str, str | None]:
    """Connect as @a client asking for an OCSP response; return the certificate received and the serial stapled for it.

    :param ats: The running Traffic Server.
    :param version: s_client protocol option, without the leading dash.
    :param client: Key in CLIENT_OPTIONS naming the key type the client accepts.
    :return: The server certificate in PEM, and the serial number in the stapled response or None if none was stapled.
    """

    result = ats.run_shell(
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} -{version} {CLIENT_OPTIONS[client][version]} "
        "-servername server.example.com -status </dev/null"
    )
    assert result.returncode == 0, result.output
    certificate = re.search(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", result.output.split("Server certificate")[-1], re.S
    )
    assert certificate is not None, result.output
    serial = re.search(r"^\s*Serial Number: ([0-9A-F]+)$", result.output, re.MULTILINE)
    return certificate.group(0), serial.group(1) if serial else None


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_tls_ocsp_cert_update(ats_factory: ATSFactory, version: str) -> None:
    """After the certificate is replaced, the replaced certificate's response is not stapled with the new one.

    The new certificate's responder never answers, so no response is stapled at all.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    replacement = replacement_file(issue(ats_factory.run_directory, "new-rsa", ("rsa:2048",), 0x6B2))
    ats = start_ats(ats_factory, [ORIGINAL], [SSL_DIRECTORY / "ocsp_response.der"])

    _, serial = stapled_serial(ats, version, "rsa")
    assert serial == serial_of(ORIGINAL), f"the original certificate's response was not stapled: {serial}"

    update_certificate(ats, replacement)
    certificate, serial = stapled_serial(ats, version, "rsa")
    assert certificate.strip() in replacement.read_text(), f"the update did not take effect: {certificate}"
    assert serial is None, f"a response for serial {serial} was stapled with the new certificate"


@pytest.mark.parametrize("version", ("tls1_2", "tls1_3"))
def test_tls_ocsp_cert_update_keeps_other_certificate(ats_factory: ATSFactory, version: str) -> None:
    """On an RSA+EC line, replacing the RSA certificate keeps the EC certificate's stapling.

    Only OpenSSL builds serve both certificates of a line from one context; elsewhere each certificate has its own
    context and the update replaces only one of them, so there is no other certificate to keep.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param version: s_client protocol option, without the leading dash.
    """

    if "BoringSSL" in ats_factory.tls_library():
        pytest.skip("each certificate of the line has its own context")
    directory = ats_factory.run_directory
    ec = issue(directory, "line-ec", ("ec", "-pkeyopt", "ec_paramgen_curve:P-256"), 0x6E1)
    replacement = replacement_file(issue(directory, "new-rsa", ("rsa:2048",), 0x6B2))
    ats = start_ats(ats_factory, [ORIGINAL, ec], [SSL_DIRECTORY / "ocsp_response.der", ocsp_response(ec, directory / "ec.der")])

    update_certificate(ats, replacement)
    certificate, serial = stapled_serial(ats, version, "rsa")
    assert certificate.strip() in replacement.read_text(), f"the RSA-only client did not get the new certificate: {certificate}"
    assert serial is None, f"a response for serial {serial} was stapled with the new certificate"
    certificate, serial = stapled_serial(ats, version, "ecdsa")
    assert certificate.strip() in ec.read_text(), f"the ECDSA-only client did not get the EC certificate: {certificate}"
    assert serial == serial_of(ec), f"the EC certificate's response was not stapled after the RSA one was replaced: {serial}"
