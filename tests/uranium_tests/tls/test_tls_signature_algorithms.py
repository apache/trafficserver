#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements. See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership. The ASF licenses this file to you under the Apache
#  License, Version 2.0 (the "License"); you may not use this file except in
#  compliance with the License. You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

from pathlib import Path
import re
import shlex
import subprocess

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def test_tls_signature_algorithms(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Log offered and negotiated algorithms for full and resumed TLS handshakes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning the HTTP origin.
    """
    version = subprocess.check_output(["openssl", "version"], text=True)
    match = re.search(r"OpenSSL (\d+)\.(\d+)\.(\d+)", version)
    if match is None or tuple(map(int, match.groups())) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    has_brainpool = tuple(map(int, match.groups())) >= (3, 2, 0)
    ats = ats_factory.create("ts", enable_tls=True)
    origin = services.origin("server")
    tls12_session = shlex.quote(str(ats.run_directory / "tls12.session"))
    tls13_session = shlex.quote(str(ats.run_directory / "tls13.session"))
    ec_session = shlex.quote(str(ats.run_directory / "tls12-ecdsa-ticket.session"))
    cases = [
        ("tls12-full", "-tls1_2", "rsa_pkcs1_sha256", f"-sess_out {tls12_session}"),
        ("tls12-resumed", "-tls1_2", "rsa_pkcs1_sha256", f"-sess_in {tls12_session}"),
        ("tls13-full", "-tls1_3", "rsa_pss_rsae_sha256:rsa_pkcs1_sha256", f"-sess_out {tls13_session}"),
        ("tls13-resumed", "-tls1_3", "rsa_pss_rsae_sha256:rsa_pkcs1_sha256", f"-sess_in {tls13_session}"),
        ("tls13-ecdsa", "-tls1_3", "ecdsa_secp256r1_sha256", ""),
        (
            "tls12-ecdsa-ticket-full", "-tls1_2", "ecdsa_secp256r1_sha256",
            f"-cipher ECDHE-ECDSA-AES128-GCM-SHA256 -sess_out {ec_session}"),
        (
            "tls12-ecdsa-ticket-resumed", "-tls1_2", "ecdsa_secp256r1_sha256",
            f"-cipher ECDHE-ECDSA-AES128-GCM-SHA256 -sess_in {ec_session}"),
    ]
    if has_brainpool:
        cases.append(("tls13-ecdsa-disambiguation", "-tls1_3", "ecdsa_secp256r1_sha256:ecdsa_brainpoolP256r1tls13_sha256", ""))
    cases.extend(
        [
            ("tls13-rsa-disambiguation", "-tls1_3", "rsa_pss_rsae_sha256:rsa_pss_pss_sha256", ""),
            ("tls12-static-rsa", "-tls1_2", "rsa_pkcs1_sha256", "-cipher AES128-GCM-SHA256"),
        ])
    for path, _, _, _ in cases:
        origin.add_response(
            {"headers": f"GET /{path} HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
    ats.copy_to_ssl(
        *(TEST_DIRECTORY / "ssl" / filename for filename in ("server.pem", "server.key", "signed-foo-ec.pem", "signed-foo-ec.key")))
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: signed-foo-ec.pem,server.pem",
            "    ssl_key_name: signed-foo-ec.key,server.key",
        ))
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    ats.records.update(
        {
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.session_ticket.number": 2,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.write_config_file(
        "logging.yaml", """logging:
  formats:
    - name: tls_signature_algorithms
      format: '%<cqup> %<cqssv> %<cqssig> %<cqssin> %<cqssr> %<cqssrt>'
  logs:
    - filename: tls_signature_algorithms
      format: tls_signature_algorithms
""")
    origin.start()
    ats.start()
    for path, tls_option, sigalgs, session_option in cases:
        request = f"GET /{path} HTTP/1.1\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n"
        result = ats.run_shell(
            f"printf '%b' {shlex.quote(request)} | openssl s_client -quiet -connect 127.0.0.1:{ats.https_port} "
            f"-servername example.com {tls_option} -sigalgs {sigalgs} {session_option}")
        assert result.returncode == 0, result.output
        assert "200 OK" in result.stdout, result.output
    log = wait_for_file_lines(ats.log_directory / "tls_signature_algorithms.log", r"^tls", len(cases))
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
    log = (ats.log_directory / "tls_signature_algorithms.log").read_text(errors="replace")
    gold = "tls_signature_algorithms" if has_brainpool else "tls_signature_algorithms_no_brainpool"
    assert_matches_gold(log, TEST_DIRECTORY / "gold" / f"{gold}.gold")
