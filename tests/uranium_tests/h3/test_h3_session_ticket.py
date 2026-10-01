#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
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
import shutil
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure QUIC and the server ticket key.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_quic=True, enable_cache=False)
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ticket_file = ats_factory.run_directory / "file.ticket"
    shutil.copy2(TEST_DIRECTORY.parent / "tls" / "file.ticket", ticket_file)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic|ssl",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.session_ticket.number": 2,
            "proxy.config.ssl.server.ticket_key.filename": str(ticket_file),
        })
    return ats


def openssl_handshake(session_option: str, *, _ats: ATS, _session_file: Path) -> None:
    """Run one QUIC handshake that saves or offers the session file.

    :param _ats: Test-local ats configured by the test.
    :param _session_file: Test-local session file configured by the test.
    :param session_option: Session option used by this test step.
    """

    result = _ats.run(
        TEST_DIRECTORY / "h3_session_ticket.sh",
        session_option,
        _session_file,
        str(_ats.https_port),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert "CONNECTION ESTABLISHED" in result.output
    assert "Protocol version: QUICv1" in result.output


def test_h3_session_ticket(ats_factory: ATSFactory) -> None:
    """HTTP/3 connections can receive and offer TLS session tickets.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    if not ats_factory.has_feature("TS_USE_QUIC"):
        pytest.skip("ATS was built without QUIC")
    version_output = subprocess.check_output(("openssl", "version"), text=True)
    version = re.search(r"\d+(?:\.\d+)+", version_output)
    if version is None or tuple(int(part) for part in version.group().split(".")) < (3, 5, 0):
        pytest.skip("OpenSSL 3.5.0 or newer is required")
    help_result = subprocess.run(("openssl", "s_client", "-help"), capture_output=True, text=True, check=False)
    if "-quic" not in help_result.stdout + help_result.stderr:
        pytest.skip("OpenSSL s_client with QUIC support is required")
    _ats = configure_ats(ats_factory)
    _session_file = ats_factory.run_directory / "h3-quic-session.pem"

    _session_file.unlink(missing_ok=True)
    _ats.start()
    openssl_handshake("-sess_out", _ats=_ats, _session_file=_session_file)
    openssl_handshake("-sess_in", _ats=_ats, _session_file=_session_file)
