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

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class TlsSessionReuseScenario:
    """Compare repeated OpenSSL connections with session tickets on and off."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._session_directory = ats_factory.run_directory
        self._origin = self.configure_origin(services)
        self._enabled = self.configure_ats(ats_factory, "ts1", enabled=True)
        self._disabled = self.configure_ats(ats_factory, "ts2", enabled=False)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the common clear-text origin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str, *, enabled: bool) -> ATS:
        """Configure one TLS endpoint with ticket issuance enabled or disabled."""

        ats = ats_factory.create(name, enable_tls=True)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.server.session_ticket.enable": int(enabled),
                "proxy.config.ssl.server.session_ticket.number": 2 if enabled else 0,
            })
        return ats

    def connection_sequence(self, ats: ATS, name: str, protocol: str, *, expect_resumable: bool) -> str:
        """Create one session and attempt five resumptions."""

        session = self._session_directory / f"{name}.dat"
        version_option = "-tls1_2" if protocol == "TLSv1.2" else "-tls1_3"
        first = (
            f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client "
            f"-connect 127.0.0.1:{ats.https_port} -sess_out {shlex.quote(str(session))} {version_option}")
        resumed = (
            f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client "
            f"-connect 127.0.0.1:{ats.https_port} -sess_in {shlex.quote(str(session))} {version_option}")
        first_result = ats.run_shell(first, timeout=30)
        assert first_result.returncode == 0, first_result.output
        if expect_resumable:
            assert session.is_file(), first_result.output
            followup = resumed
        elif session.is_file():
            followup = resumed
        else:
            followup = (
                f"printf 'GET / HTTP/1.1\\r\\n\\r\\n' | openssl s_client "
                f"-connect 127.0.0.1:{ats.https_port} {version_option}")
        result = ats.run_shell(" && ".join([followup] * 5), timeout=60)
        assert result.returncode == 0, result.output
        output = first_result.output + result.output
        assert protocol in output
        return output

    def run(self) -> None:
        """Verify five resumptions when enabled and none when disabled."""

        self._origin.start()
        self._enabled.start()
        self._disabled.start()
        enabled_first = self.connection_sequence(self._enabled, "sess1", "TLSv1.2", expect_resumable=True)
        enabled_second = self.connection_sequence(self._enabled, "sess2", "TLSv1.2", expect_resumable=True)
        assert enabled_first.count("Reused, TLSv1.2") == 5
        assert enabled_second.count("Reused, TLSv1.2") == 5
        disabled_tls12 = self.connection_sequence(self._disabled, "sess3", "TLSv1.2", expect_resumable=False)
        disabled_tls13 = self.connection_sequence(self._disabled, "sess4", "TLSv1.3", expect_resumable=False)
        assert "Reused" not in disabled_tls12
        assert "Reused" not in disabled_tls13


def test_tls_session_reuse(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS reuses ticket sessions only when ticket issuance is enabled."""

    TlsSessionReuseScenario(ats_factory, services).run()
