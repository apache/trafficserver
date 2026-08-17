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

import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class SslMulticertLoaderScenario:
    """Exercise failed reload retention, startup failure, and parallel loading."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    @staticmethod
    def configure_origin(services: ServiceFactory, name: str) -> OriginServer:
        """Create an origin for the TLS listener smoke requests."""

        origin = services.origin(name)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    @staticmethod
    def configure_valid_ats(ats_factory: ATSFactory, name: str, origin: OriginServer) -> ATS:
        """Configure a TLS listener with the default certificate."""

        ats = ats_factory.create(name, enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.multicert.exit_on_load_fail": 0,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port}")
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        return ats

    def request(self, ats: ATS) -> None:
        """Require the configured example.com certificate and a good response."""

        result = self._curl.run_for(
            ats,
            (
                f"--silent --verbose --insecure --resolve 'example.com:{ats.https_port}:127.0.0.1' "
                f"'https://example.com:{ats.https_port}/'"),
        )
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.stdout
        assert "CN=example.com" in result.stderr

    def check_failed_reload_retains_old_context(self) -> None:
        """Fail a certificate reload and verify the old context still serves."""

        origin = self.configure_origin(self._services, "server")
        ats = self.configure_valid_ats(self._ats_factory, "ts", origin)
        origin.start()
        ats.start()
        self.request(ats)

        ats.write_config_file(
            "ssl_multicert.yaml",
            "ssl_multicert:\n"
            "  - ssl_cert_name: server_does_not_exist.pem\n"
            "    ssl_key_name: server_does_not_exist.key\n"
            '  - dest_ip: "*"\n'
            "    ssl_cert_name: server.pem_doesnotexist\n"
            "    ssl_key_name: server.key\n",
        )
        reload_result = ats.traffic_ctl("config", "reload", "-t", "invalid_multicert")
        assert reload_result.returncode == 0, reload_result.output
        time.sleep(3)
        self.request(ats)
        diagnostics = ats.diags_log.read_text(errors="replace")
        assert "(quic)" not in "\n".join(line for line in diagnostics.splitlines() if "ssl_multicert" in line)

    def check_invalid_startup_fails(self) -> None:
        """Require the default exit-on-load-failure behavior."""

        ats = self._ats_factory.create("ts2", enable_tls=True)
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem_doesnotexist",
                "    ssl_key_name: server.key",
            ))
        ats.expect_start_failure("EMERGENCY: failed to load SSL certificate file", return_code=33)
        ats.start()

    def check_parallel_loading(self) -> None:
        """Load multiple certificate entries during startup."""

        origin = self.configure_origin(self._services, "server3")
        ats = self.configure_valid_ats(self._ats_factory, "ts3", origin)
        ats.ssl_multicert_config.add_lines((
            "  - ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
        origin.start()
        ats.start()
        self.request(ats)
        assert "loaded 2 certs" in ats.diags_log.read_text(errors="replace")

    def run(self) -> None:
        """Run every ssl_multicert loader behavior."""

        self.check_failed_reload_retains_old_context()
        self.check_invalid_startup_fails()
        self.check_parallel_loading()


def test_ssl_multicert_loader(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Certificate reload failures retain old contexts and startup remains strict."""

    SslMulticertLoaderScenario(ats_factory, services, curl).run()
