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

from tools.uranium.services import ATS, ATSFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Install paired certificates and prefer the ECDSA cipher.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    names = (
        "signed-foo.pem",
        "signed-foo.key",
        "signed-foo-ec.pem",
        "signed-foo-ec.key",
        "signed-san.pem",
        "signed-san.key",
        "signed-san-ec.pem",
        "signed-san-ec.key",
        "signer.pem",
        "signer.key",
        "server.pem",
        "server.key",
    )
    ats.copy_to_ssl(*(SSL_DIRECTORY / name for name in names))
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            "  - ssl_cert_name: signed-foo-ec.pem,signed-foo.pem",
            "    ssl_key_name: signed-foo-ec.key,signed-foo.key",
            "  - ssl_cert_name: signed-san-ec.pem,signed-san.pem",
            "    ssl_key_name: signed-san-ec.key,signed-san.key",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.cipher_suite": ("ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256"),
            "proxy.config.diags.debug.tags": "ssl",
            "proxy.config.diags.debug.enabled": 1,
        })
    return ats


def certificate_prefix(name: str) -> str:
    """Return the PEM certificate portion before its end marker.

    :param name: Unique service or case name within this test.
    """

    content = (SSL_DIRECTORY / name).read_text()
    return content[:content.index("END CERTIFICATE-----")]


def handshake(hostname: str, *, rsa_only: bool = False, _ats: ATS) -> str:
    """Perform a TLS 1.2 handshake for @a hostname.

    :param _ats: Test-local ats configured by the test.
    :param hostname: Host name used for certificate or route selection.
    :param rsa_only: Rsa only used by this test step.
    """

    cipher = " -cipher ECDHE-RSA-AES128-GCM-SHA256" if rsa_only else ""
    script = (
        f"printf 'foo\\n' | openssl s_client -tls1_2 -servername {shlex.quote(hostname)}"
        f"{cipher} -connect 127.0.0.1:{_ats.https_port}")
    result = _ats.run_shell(script)
    assert result.returncode == 0, result.output
    return result.output


def test_tls_check_dual_cert_selection(ats_factory: ATSFactory) -> None:
    """ATS selects the matching ECDSA or RSA certificate for each client.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    assert certificate_prefix("signed-foo-ec.pem") in handshake("foo.com", _ats=_ats)
    assert certificate_prefix("signed-foo.pem") in handshake("foo.com", rsa_only=True, _ats=_ats)

    san_ec = handshake("two.com", _ats=_ats)
    assert certificate_prefix("signed-san-ec.pem") in san_ec
    assert "CN=group.com" in san_ec

    san_rsa = handshake("two.com", rsa_only=True, _ats=_ats)
    assert certificate_prefix("signed-san.pem") in san_rsa
    assert "CN=group.com" in san_rsa

    rsa_only = handshake("rsa.com", _ats=_ats)
    assert certificate_prefix("signed-san.pem") in rsa_only
    assert "CN=group.com" in rsa_only

    ec_only = handshake("ec.com", _ats=_ats)
    assert certificate_prefix("signed-san-ec.pem") in ec_only
    assert "CN=group.com" in ec_only
