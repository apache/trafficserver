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
"""TLS tickets must retain their SNI, verification policy, and trust scope."""
import os
import re
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest
from tools.uranium.services import ATS, ATSFactory, ServiceFactory
from tools.uranium.services import CommandResult
from tools.uranium.utils import version_tuple

DIRECTORY = Path(__file__).parent
SSL = DIRECTORY / "ssl"


def configure_ticket_proxy(ats_factory: ATSFactory, services: ServiceFactory) -> ATS:
    """Start an origin and configure one wildcard certificate shared by all SNIs.

    :param ats_factory: Isolated proxy factory.
    :param services: Origin process factory.
    """
    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    if version_tuple(subprocess.check_output(["openssl", "version"], text=True)) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"}, {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n",
            "body": "hello"
        })
    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(
        SSL / "server.pem", SSL / "server.key", SSL / "signer.pem", SSL / "aaa-ca.pem", SSL / "bbb-ca.pem", SSL / "intca-root.pem")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]})
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    ats.records.update(
        {
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.ticket_key.filename": str(DIRECTORY / "file.ticket"),
            "proxy.config.ssl.TLSv1_2": 1,
            "proxy.config.ssl.TLSv1_3.enabled": 1,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.write_config_file(
        "logging.yaml", """
logging:
  formats:
    - name: testformat
      format: '%<pssc> %<{Host}cqh>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
""")
    origin.start()
    return ats


def connect(
        ats: ATS,
        sni: str,
        session: Path,
        *,
        save: bool = False,
        version: str = "1.3",
        certificate: str | None = None,
        chain: str | None = None) -> CommandResult:
    """Send one request while saving or offering a TLS session.

    :param ats: Running proxy.
    :param sni: TLS server name and HTTP Host.
    :param session: Sandbox session file to save or offer.
    :param save: Save a fresh session instead of offering an existing one.
    :param version: TLS version, either 1.2 or 1.3.
    :param certificate: Client certificate/key basename, or None to omit both.
    :param chain: Optional intermediate certificate filename.
    """
    request = f"GET / HTTP/1.1\r\nHost: {sni}\r\nConnection: close\r\n\r\n"
    args = [
        "openssl", "s_client", "-connect", f"127.0.0.1:{ats.https_port}", "-servername", sni, f"-tls1_{version[-1]}", "-ign_eof",
        "-sess_out" if save else "-sess_in",
        str(session)
    ]
    if certificate:
        args += ["-cert", str(SSL / f"{certificate}.pem"), "-key", str(SSL / f"{certificate}.key")]
    if chain:
        args += ["-cert_chain", str(SSL / chain)]
    result = ats.run_shell(f"printf %s {shlex.quote(request)} | {shlex.join(args)}", timeout=30)
    if save:
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 200" in result.output, result.output
        assert session.stat().st_size > 0
    return result


def assert_resumed(result: CommandResult, version: str = "1.3") -> None:
    """Require both a negotiated resumption and a served HTTP request.

    :param result: OpenSSL command result.
    :param version: Expected TLS version.
    """
    assert result.returncode == 0, result.output
    assert f"Reused, TLSv{version}" in result.output, result.output
    assert "HTTP/1.1 200" in result.output, result.output


def assert_access_counts(ats: ATS, counts: dict[str, int]) -> None:
    """Flush logs and require exact successful-request counts per hostname.

    :param ats: Proxy to stop gracefully before examining its access log.
    :param counts: Expected counts, including zero for forbidden hostnames.
    """
    ats.stop()
    content = (ats.log_directory / "squid.log").read_text()
    for host, count in counts.items():
        assert len(re.findall(rf"^200 {re.escape(host)}$", content, re.MULTILINE)) == count, content


def test_tls_resume_cross_sni(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Bind sessions to SNI even without a verify_client policy.

    :param ats_factory: Proxy factory.
    :param services: Origin factory.
    """
    ats = configure_ticket_proxy(ats_factory, services)
    ats.start()
    for version in ("1.2", "1.3"):
        session = ats.run_directory / f"session-{version}"
        connect(ats, "alpha.example.com", session, save=True, version=version)
        assert_resumed(connect(ats, "alpha.example.com", session, version=version), version)
        result = connect(ats, "beta.example.com", session, version=version)
        assert result.returncode == 0, result.output
        assert "Reused" not in result.output, result.output
        assert "HTTP/1.1 200" in result.output, result.output
    session = ats.run_directory / "session-case-fold"
    connect(ats, "alpha.example.com", session, save=True)
    assert_resumed(connect(ats, "ALPHA.EXAMPLE.COM", session))


@pytest.mark.parametrize("different_ca", (False, True), ids=("verify-client-scope", "verify-client-ca-scope"))
def test_tls_ticket_verify_client_scope(ats_factory: ATSFactory, services: ServiceFactory, different_ca: bool) -> None:
    """Refuse cross-policy tickets but resume valid same-policy sessions.

    :param ats_factory: Proxy factory.
    :param services: Origin factory.
    :param different_ca: Use two STRICT tenants with different CAs instead of NONE and STRICT.
    """
    ats = configure_ticket_proxy(ats_factory, services)
    if different_ca:
        first, second, cert = "aaa.example.com", "bbb.example.com", "aaa-signed"
        ats.write_config_file(
            "sni.yaml", f"""sni:
- fqdn: {first}
  verify_client: STRICT
  verify_client_ca_certs:
    file: {ats.ssl_directory}/aaa-ca.pem
- fqdn: {second}
  verify_client: STRICT
  verify_client_ca_certs:
    file: {ats.ssl_directory}/bbb-ca.pem
""")
    else:
        first, second, cert = "open.example.com", "strict.example.com", "signed-foo"
        ats.records.update({"proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem")})
        ats.write_config_file(
            "sni.yaml", f"""sni:
- fqdn: {first}
  verify_client: NONE
- fqdn: {second}
  verify_client: STRICT
""")
    ats.start()
    session = ats.run_directory / "cross-session"
    connect(ats, first, session, save=True, certificate=cert if different_ca else None)
    rejected = connect(ats, second, session)
    assert "Reused" not in rejected.output, rejected.output
    assert "HTTP/1.1 200" not in rejected.output, rejected.output
    control = first if different_ca else second
    session = ats.run_directory / "control-session"
    connect(ats, control, session, save=True, certificate=cert)
    assert_resumed(connect(ats, control, session))
    assert_access_counts(ats, {first: 3, second: 0} if different_ca else {first: 1, second: 2})


def test_tls_resume_verify_level(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Changing only verify_client retires the SNI's old sessions.

    :param ats_factory: Proxy factory.
    :param services: Origin factory.
    """
    ats = configure_ticket_proxy(ats_factory, services)
    policy = f"""sni:
- fqdn: level.example.com
  verify_client: MODERATE
  verify_client_ca_certs:
    file: {ats.ssl_directory}/aaa-ca.pem
"""
    ats.write_config_file("sni.yaml", policy)
    ats.start()
    session = ats.run_directory / "session"
    connect(ats, "level.example.com", session, save=True, certificate="aaa-signed")
    assert_resumed(connect(ats, "level.example.com", session, certificate="aaa-signed"))
    (ats.config_directory / "sni.yaml").write_text(policy.replace("MODERATE", "STRICT"))
    result = ats.traffic_ctl("config", "reload", "-m", "-t", "verify-level", "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode == 0, result.output
    result = connect(ats, "level.example.com", session, certificate="aaa-signed")
    assert result.returncode == 0, result.output
    assert "Reused" not in result.output, result.output
    assert "HTTP/1.1 200" in result.output, result.output


@pytest.mark.parametrize("intermediate", (False, True), ids=("global-ca-reload", "intermediate-ca"))
def test_tls_resume_ca_replacement(ats_factory: ATSFactory, services: ServiceFactory, intermediate: bool) -> None:
    """Changing trust-bundle contents retires sessions without changing its path.

    :param ats_factory: Proxy factory.
    :param services: Origin factory.
    :param intermediate: Verify a transmitted intermediate chain using the per-SNI root bundle.
    """
    ats = configure_ticket_proxy(ats_factory, services)
    host = "int.example.com" if intermediate else "strict.example.com"
    bundle = ats.ssl_directory / ("intca-root.pem" if intermediate else "aaa-ca.pem")
    policy = f"sni:\n- fqdn: {host}\n  verify_client: STRICT\n"
    if intermediate:
        policy += f"  verify_client_ca_certs:\n    file: {bundle}\n"
    else:
        ats.records.update({"proxy.config.ssl.CA.cert.filename": str(bundle)})
    ats.write_config_file("sni.yaml", policy)
    ats.start()
    cert = "intclient" if intermediate else "aaa-signed"
    chain = "intca-sub.pem" if intermediate else None
    versions = ("1.2", "1.3") if intermediate else ("1.3",)
    for version in versions:
        session = ats.run_directory / f"session-{version}"
        connect(ats, host, session, save=True, version=version, certificate=cert, chain=chain)
        assert_resumed(connect(ats, host, session, version=version, certificate=cert, chain=chain), version)
    old_mtime = bundle.stat().st_mtime
    shutil.copyfile(SSL / ("aaa-ca.pem" if intermediate else "bbb-ca.pem"), bundle)
    if not intermediate:
        # The global bundle is cached in SSL_CTX and needs an observable reload.
        os.utime(bundle, (old_mtime + 2, old_mtime + 2))
        result = ats.traffic_ctl("config", "reload", "-m", "-t", "replace-ca", "-w", "1", "-r", "0.5", "-T", "30s")
        assert result.returncode == 0, result.output
    for version in versions:
        result = connect(ats, host, ats.run_directory / f"session-{version}", version=version, certificate=cert, chain=chain)
        assert f"New, TLSv{version}" in result.output, result.output
        assert "tlsv1 alert unknown ca" in result.output, result.output
        assert "Reused" not in result.output, result.output
        assert "HTTP/1.1 200" not in result.output, result.output
    assert_access_counts(ats, {host: 2 * len(versions)})
    if intermediate:
        assert "rejecting a resumed TLS session" not in ats.diags_log.read_text()
