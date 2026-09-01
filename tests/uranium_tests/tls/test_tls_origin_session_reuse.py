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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class OriginSessionReuseScenario:
    """Exercise the outbound TLS session cache and its size limit."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ts1 = self.configure_tls_origin(ats_factory, "ts1", reuse=True)
        self._ts2 = self.configure_proxy(ats_factory, "ts2", reuse=True)
        self._ts3 = self.configure_tls_origin(ats_factory, "ts3", reuse=True)
        self._ts4 = self.configure_proxy(ats_factory, "ts4", reuse=False)
        self.configure_remaps()

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the final clear-text origin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "curl test"
            },
        )
        return origin

    @staticmethod
    def configure_tls(ats: ATS, *, reuse: bool, debug: bool = False) -> None:
        """Apply the shared TLS session settings to one ATS instance."""

        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.http.cache.http": 0,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.ssl.server.session_ticket.enable": 1,
                "proxy.config.ssl.origin_session_cache.enabled": int(reuse),
                "proxy.config.ssl.origin_session_cache.size": 1,
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.diags.debug.enabled": int(debug),
                "proxy.config.diags.debug.tags": "ssl.origin_session_cache",
            })

    def configure_tls_origin(self, ats_factory: ATSFactory, name: str, *, reuse: bool) -> ATS:
        """Create an ATS TLS endpoint in front of the clear-text origin."""

        ats = ats_factory.create(name, enable_tls=True)
        self.configure_tls(ats, reuse=reuse)
        return ats

    def configure_proxy(self, ats_factory: ATSFactory, name: str, *, reuse: bool) -> ATS:
        """Create an ATS instance whose outbound TLS cache is under test."""

        ats = ats_factory.create(name, enable_tls=True)
        self.configure_tls(ats, reuse=reuse, debug=True)
        return ats

    def configure_remaps(self) -> None:
        """Connect the four ATS instances into cached and disabled pairs."""

        self._ts1.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}")
        self._ts2.remap_config.add_lines(
            (
                f"map /reuse_session https://127.0.0.1:{self._ts1.https_port}",
                f"map /remove_oldest https://127.0.1.1:{self._ts1.https_port}",
            ))
        self._ts3.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}")
        self._ts4.remap_config.add_line(f"map / https://127.0.0.1:{self._ts3.https_port}")

    def request_twice(self, ats: ATS, path: str = "") -> None:
        """Use two client connections to create two outbound TLS sessions."""

        for _ in range(2):
            result = self._curl.run_for(
                ats,
                f"--insecure 'https://127.0.0.1:{ats.https_port}/{path}'",
            )
            assert result.returncode == 0, result.output
            assert "curl test" in result.stdout

    def run(self) -> None:
        """Verify reuse, eviction, and disabled-cache behavior."""

        self._origin.start()
        for ats in (self._ts1, self._ts2, self._ts3, self._ts4):
            ats.start()

        self.request_twice(self._ts2, "reuse_session")
        self.request_twice(self._ts2, "remove_oldest")
        enabled_log = self._ts2.traffic_out.read_text(errors="replace")
        assert "new session to origin" in enabled_log
        assert "reused session to origin" in enabled_log
        assert "remove oldest session" in enabled_log

        self.request_twice(self._ts4)
        disabled_log = self._ts4.traffic_out.read_text(errors="replace")
        assert "new session to origin" in disabled_log
        assert "reused session to origin" not in disabled_log


def test_tls_origin_session_reuse(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The outbound TLS session cache reuses, evicts, and disables sessions."""

    OriginSessionReuseScenario(ats_factory, services, curl).run()
