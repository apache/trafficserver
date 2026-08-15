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
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsSniTicketScenario:
    """Override process-wide TLS session ticket policy for individual SNI names."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._enabled = self.configure_ats(
            ats_factory,
            "tickets-enabled",
            "tickets-on.com",
            global_enabled=0,
            global_count=0,
            sni_enabled=1,
            sni_count=3,
        )
        self._disabled = self.configure_ats(
            ats_factory,
            "tickets-disabled",
            "tickets-off.com",
            global_enabled=1,
            global_count=2,
            sni_enabled=0,
        )

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the response endpoint used by every ticket handshake."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: tickets.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "ticket test",
            },
        )
        return origin

    def configure_ats(
        self,
        ats_factory: ATSFactory,
        name: str,
        sni: str,
        *,
        global_enabled: int,
        global_count: int,
        sni_enabled: int,
        sni_count: int | None = None,
    ) -> ATS:
        """Configure one process-wide policy and its per-SNI override."""

        ats = ats_factory.create(name, enable_tls=True)
        ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key")
        ats.copy_to_config(TEST_DIRECTORY / "file.ticket")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl|http",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.server.session_ticket.enable": global_enabled,
                "proxy.config.ssl.server.session_ticket.number": global_count,
                "proxy.config.ssl.server.ticket_key.filename": str(ats.config_directory / "file.ticket"),
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        document = "sni:\n" f"  - fqdn: {sni}\n" f"    ssl_ticket_enabled: {sni_enabled}\n"
        if sni_count is not None:
            document += f"    ssl_ticket_number: {sni_count}\n"
        ats.write_config_file("sni.yaml", document)
        return ats

    @staticmethod
    def tls12_reuse(ats: ATS, servername: str) -> str:
        """Create one session and attempt to resume it five times."""

        session = ats.run_directory / "session.pem"
        request = f"GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n"
        first = (
            f"printf '{request}' | openssl s_client -connect 127.0.0.1:{ats.https_port} "
            f"-servername {servername} -sess_out '{session}' -tls1_2")
        reuse = (
            f"printf '{request}' | openssl s_client -connect 127.0.0.1:{ats.https_port} "
            f"-servername {servername} -sess_in '{session}' -tls1_2")
        result = ats.run_shell(" && ".join((first, reuse, reuse, reuse, reuse, reuse)), timeout=45)
        assert result.returncode == 0, result.output
        return result.output

    @staticmethod
    def tls13_messages(ats: ATS, servername: str) -> str:
        """Return OpenSSL's TLSv1.3 protocol-message trace for one connection."""

        request = f"GET / HTTP/1.1\\r\\nHost: {servername}\\r\\nConnection: close\\r\\n\\r\\n"
        command = (
            f"printf '{request}' | openssl s_client -connect 127.0.0.1:{ats.https_port} "
            f"-servername {servername} -tls1_3 -msg -ign_eof")
        result = ats.run_shell(command, timeout=30)
        assert result.returncode == 0, result.output
        return result.output

    @staticmethod
    def tls12_reconnect(ats: ATS, servername: str) -> str:
        """Ask OpenSSL to reconnect repeatedly when no session tickets are issued."""

        command = (
            f"openssl s_client -connect 127.0.0.1:{ats.https_port} "
            f"-servername {servername} -tls1_2 -reconnect </dev/null")
        result = ats.run_shell(command, timeout=30)
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Verify TLSv1.2 resumption and TLSv1.3 ticket counts for both overrides."""

        version = subprocess.run(("openssl", "version"), capture_output=True, text=True, check=False).stdout
        if "OpenSSL" not in version and "BoringSSL" not in version:
            pytest.skip("OpenSSL-compatible s_client is required")

        self._origin.start()
        self._enabled.start()
        self._disabled.start()

        enabled12 = self.tls12_reuse(self._enabled, "tickets-on.com")
        assert enabled12.count("Reused, TLSv1.2") == 5
        disabled12 = self.tls12_reconnect(self._disabled, "tickets-off.com")
        assert "Reused" not in disabled12
        assert "TLSv1.2" in disabled12

        enabled13 = self.tls13_messages(self._enabled, "tickets-on.com")
        expected_tickets = 0 if "BoringSSL" in version else 3
        assert enabled13.count("NewSessionTicket") == expected_tickets
        disabled13 = self.tls13_messages(self._disabled, "tickets-off.com")
        assert "NewSessionTicket" not in disabled13


def test_tls_sni_ticket(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Per-SNI ticket overrides take precedence over process-wide TLS ticket settings."""

    TlsSniTicketScenario(ats_factory, services).run()
