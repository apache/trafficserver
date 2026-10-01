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
import re
import shlex
import shutil

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the reusable empty-response origin.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, name: str, *, _origin: OriginServer) -> ATS:
    """Configure one TLS endpoint to read the shared ticket key.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    """

    ats = ats_factory.create(name, enable_tls=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.ticket_key.filename": "../../file.ticket",
        })
    return ats


def openssl_request(ats: ATS, *, resume: bool, _ticket: Path) -> str:
    """Create or resume the session and return OpenSSL's diagnostic output.

    :param _ticket: Test-local ticket configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param resume: Resume used by this test step.
    """

    session_option = f"-sess_in {shlex.quote(str(_ticket))}" if resume else f"-sess_out {shlex.quote(str(_ticket))}"
    result = ats.run_shell(
        f"printf 'GET / HTTP/1.0\\r\\n\\r\\n' | openssl s_client -tls1_2 "
        f"-connect 127.0.0.1:{ats.https_port} {session_option}",
        timeout=30,
    )
    assert result.returncode == 0, result.output
    return result.output


def test_tls_ticket(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A shared TLS ticket key resumes the same session on another ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _first = configure_ats(ats_factory, "ts", _origin=_origin)
    _second = configure_ats(ats_factory, "ts2", _origin=_origin)
    _ticket = _first.run_directory.parent / "ticket.out"

    shutil.copy2(TEST_DIRECTORY / "file.ticket", _first.run_directory.parent / "file.ticket")
    _origin.start()
    _first.start()
    _second.start()
    first = openssl_request(_first, resume=False, _ticket=_ticket)
    second = openssl_request(_second, resume=True, _ticket=_ticket)
    first_ids = re.findall(r"Session-ID: ([0-9A-F]+)", first)
    second_ids = re.findall(r"Session-ID: ([0-9A-F]+)", second)
    assert first_ids and second_ids, f"Missing TLS session id:\n{first}\n{second}"
    assert first_ids[0] == second_ids[0]
