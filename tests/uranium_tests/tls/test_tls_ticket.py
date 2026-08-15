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


class TlsTicketScenario:
    """Resume a TLS 1.2 session across ATS instances sharing a ticket key."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._first = self.configure_ats(ats_factory, "ts")
        self._second = self.configure_ats(ats_factory, "ts2")
        self._ticket = self._first.run_directory.parent / "ticket.out"

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the reusable empty-response origin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str) -> ATS:
        """Configure one TLS endpoint to read the shared ticket key."""

        ats = ats_factory.create(name, enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.server.session_ticket.enable": 1,
                "proxy.config.ssl.server.ticket_key.filename": "../../file.ticket",
            })
        return ats

    def openssl_request(self, ats: ATS, *, resume: bool) -> str:
        """Create or resume the session and return OpenSSL's diagnostic output."""

        session_option = f"-sess_in {shlex.quote(str(self._ticket))}" if resume else f"-sess_out {shlex.quote(str(self._ticket))}"
        result = ats.run_shell(
            f"printf 'GET / HTTP/1.0\\r\\n\\r\\n' | openssl s_client -tls1_2 "
            f"-connect 127.0.0.1:{ats.https_port} {session_option}",
            timeout=30,
        )
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Create a ticket on one ATS and resume it on the second."""

        shutil.copy2(TEST_DIRECTORY / "file.ticket", self._first.run_directory.parent / "file.ticket")
        self._origin.start()
        self._first.start()
        self._second.start()
        first = self.openssl_request(self._first, resume=False)
        second = self.openssl_request(self._second, resume=True)
        first_ids = re.findall(r"Session-ID: ([0-9A-F]+)", first)
        second_ids = re.findall(r"Session-ID: ([0-9A-F]+)", second)
        assert first_ids and second_ids, f"Missing TLS session id:\n{first}\n{second}"
        assert first_ids[0] == second_ids[0]


def test_tls_ticket(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A shared TLS ticket key resumes the same session on another ATS."""

    TlsTicketScenario(ats_factory, services).run()
