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
import sys

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    OriginServer,
    ProcessService,
    ServiceFactory,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


class TunnelActiveTimeoutScenario:
    """Hold a CONNECT tunnel open beyond ATS's active timeout."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Provide the TLS endpoint reached through the tunnel."""

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: server\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "hello"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow the CONNECT target and configure a two-second active timeout."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|ssl|tunnel",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.http.connect_ports": str(self._origin.https_port),
                "proxy.config.http.transaction_active_timeout_in": 2,
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        ats.remap_config.add_line(f"map / https://127.0.0.1:{self._origin.https_port}")
        ats.allow_private_connect()
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "custom",
                            "format": "%<crc> %<pssc> %<cqhm>"
                        }],
                        "logs": [{
                            "filename": "squid.log",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Establish the tunnel and leave it idle long enough to expire."""

        return services.process(
            "client",
            (
                sys.executable,
                TEST_DIRECTORY / "tunnel_timeout_client.py",
                "127.0.0.1",
                str(self._ats.http_port),
                "127.0.0.1",
                str(self._origin.https_port),
                "5",
            ),
        )

    @staticmethod
    def verify_client(result: CommandResult) -> None:
        """Require the client to observe and tolerate the timeout closure."""

        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Run the tunnel and verify its Squid result code."""

        self._origin.start()
        self._ats.start()
        self.verify_client(self._client.run(timeout=15))
        wait_for_file_lines(self._ats.log_directory / "squid.log", r"ERR_TUN_ACTIVE_TIMEOUT.*CONNECT", 1, timeout=10)


def test_tunnel_active_timeout(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Tunnel expiration is logged as ERR_TUN_ACTIVE_TIMEOUT."""

    TunnelActiveTimeoutScenario(ats_factory, services).run()
