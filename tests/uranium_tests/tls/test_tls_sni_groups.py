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
import subprocess
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl

TEST_DIRECTORY = Path(__file__).parent


def openssl_at_least(required: tuple[int, ...]) -> bool:
    """Return whether the runtime OpenSSL version is at least @a required.

    :param required: Required used by this test step.
    """

    output = subprocess.check_output(("openssl", "version"), text=True)
    match = re.search(r"\d+(?:\.\d+)+", output)
    return match is not None and tuple(int(part) for part in match.group().split(".")) >= required


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure TLS group and cipher policy for three SNI names.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, disable_log_checks=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl_sni",
    })
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "- fqdn: aaa.com\n"
        "  server_groups_list: X25519MLKEM768\n"
        "  valid_tls_versions_in: [ TLSv1_3 ]\n"
        "  server_TLSv1_3_cipher_suites: TLS_AES_256_GCM_SHA384\n"
        "- fqdn: bbb.com\n"
        "  server_groups_list: x25519\n"
        "  valid_tls_versions_in: [ TLSv1_2 ]\n"
        "  server_cipher_suite: ECDHE-RSA-AES256-GCM-SHA384\n"
        "- fqdn: ccc.com\n"
        "  server_groups_list: ABC123\n"
        "  valid_tls_versions_in: [ TLSv1_2 ]\n"
        "  server_cipher_suite: ECDHE-RSA-AES256-GCM-SHA384\n",
    )
    return ats


def request(hostname: str, *cipher_options: str, _ats: ATS, _curl: Curl) -> str:
    """Run curl with @a hostname and selected cipher options.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param hostname: Host name used for certificate or route selection.
    :param cipher_options: Cipher options used by this test step.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--verbose {shlex.join(cipher_options)} --resolve '{hostname}:{_ats.https_port}:127.0.0.1' "
            f"--insecure 'https://{hostname}:{_ats.https_port}'"),
    )
    if hostname == "ccc.com":
        assert result.returncode == 35, result.output
    else:
        assert result.returncode == 0, result.output
    return result.output


NEGOTIATED_VERSION = re.compile(r"New, (TLSv1\.\d),")


def s_client(hostname: str, *options: str, _ats: ATS) -> CommandResult:
    """Send one request through ``openssl s_client`` with @a hostname as SNI.

    :param hostname: SNI and Host header value.
    :param options: Additional ``s_client`` protocol and cipher options.
    :param _ats: Test-local ats configured by the test.
    """

    return _ats.run_shell(
        f'printf "GET / HTTP/1.1\\r\\nHost: {hostname}\\r\\nConnection: close\\r\\n\\r\\n" | '
        f'openssl s_client -connect 127.0.0.1:{_ats.https_port} -servername {hostname} {shlex.join(options)} -ign_eof')


def require_version(result: CommandResult, version: str) -> None:
    """Require a completed handshake at exactly @a version.

    :param result: Completed ``s_client`` run.
    :param version: Expected negotiated protocol, such as ``TLSv1.2``.
    """

    assert result.returncode == 0, result.output
    match = NEGOTIATED_VERSION.search(result.output)
    assert match is not None and match.group(1) == version, result.output


def require_refused(result: CommandResult) -> None:
    """Require that no TLS session was negotiated.

    :param result: Completed ``s_client`` run.
    """

    assert result.returncode != 0, result.output
    assert NEGOTIATED_VERSION.search(result.output) is None, result.output


def test_tls_sni_groups(ats_factory: ATSFactory, curl: Curl) -> None:
    """SNI policy selects supported TLS groups and versions and rejects invalid ones.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("TLS SNI handshake coverage requires a TCP listener")
    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if not openssl_at_least((1, 1, 1)):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    _ats = configure_ats(ats_factory)

    _ats.start()
    result = s_client("bbb.com", "-cipher", "ECDHE-RSA-AES256-GCM-SHA384", _ats=_ats)
    require_version(result, "TLSv1.2")
    assert re.search(r"(Server|Peer) Temp Key: X25519", result.output), result.output
    require_refused(s_client("bbb.com", "-tls1_3", _ats=_ats))
    request("ccc.com", "--ciphers", "ECDHE-RSA-AES256-GCM-SHA384", _ats=_ats, _curl=curl)

    traffic_out = _ats.traffic_out.read_text(errors="replace")
    assert "Setting groups list from server_groups_list to x25519" in traffic_out
    assert "ERROR: Invalid server_groups_list: ABC123" in _ats.diags_log.read_text(errors="replace")

    require_refused(s_client("aaa.com", "-tls1_2", _ats=_ats))
    if openssl_at_least((3, 5, 0)):
        # System crypto policies, such as RHEL's, may leave X25519MLKEM768 out
        # of the groups s_client offers by default.
        result = s_client("aaa.com", "-ciphersuites", "TLS_AES_256_GCM_SHA384", "-groups", "X25519MLKEM768", _ats=_ats)
        require_version(result, "TLSv1.3")
        assert re.search(r"((Server|Peer) Temp Key|Negotiated TLS1\.3 group): X25519MLKEM768", result.output), result.output
        traffic_out = _ats.traffic_out.read_text(errors="replace")
        assert "Setting groups list from server_groups_list to X25519MLKEM768" in traffic_out
