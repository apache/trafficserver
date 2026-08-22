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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_metric

TEST_DIRECTORY = Path(__file__).parent


class TransactionTypeScenario:
    """Verify the transaction types reported to a test plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("Transaction type coverage requires TCP client connections")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the TLS origin used by HTTP, SNI tunnel, and CONNECT traffic."""

        origin = services.origin("origin", ssl=True)
        response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"}
        origin.add_response({"headers": "GET / HTTP/1.1\r\nHost: http-test\r\n\r\n"}, response)
        origin.add_response({"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n"}, response)
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the test plugin and three TLS routing paths."""

        ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.pem")
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.key")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/hook_tunnel_plugin.so")
        ats.plugin_config.add_line("hook_tunnel_plugin.so")
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.http.connect_ports": str(self._origin.https_port),
            })
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.remap_config.add_line(f"map https://http-test:{ats.https_port}/ https://127.0.0.1:{self._origin.https_port}/")
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: tunnel-test\n"
            f"    tunnel_route: localhost:{self._origin.https_port}\n",
        )
        ats.allow_private_connect(("CONNECT", "GET"))
        return ats

    def request(self, *arguments: str) -> None:
        """Run a curl request and require a successful exchange."""

        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output

    def send_traffic(self) -> None:
        """Send ordinary HTTP, SNI tunnel, and CONNECT transactions."""

        common = ("--insecure", "--http1.1", "--header", "Connection: close", "--verbose", "--silent")
        self.request(
            *common,
            "--resolve",
            f"http-test:{self._ats.https_port}:127.0.0.1",
            f"https://http-test:{self._ats.https_port}/",
        )
        self.request(
            *common,
            "--resolve",
            f"tunnel-test:{self._ats.https_port}:127.0.0.1",
            f"https://tunnel-test:{self._ats.https_port}/",
        )
        self.request(
            *common,
            "--resolve",
            f"connect-proxy:{self._ats.http_port}:127.0.0.1",
            "--proxy",
            f"http://connect-proxy:{self._ats.http_port}",
            "--resolve",
            f"http-test:{self._ats.https_port}:127.0.0.1",
            f"https://http-test:{self._ats.https_port}/",
        )

    def run(self) -> None:
        """Drive traffic, signal completion, and verify plugin metrics."""

        self._origin.start()
        self._ats.start()
        self.send_traffic()
        result = self._ats.traffic_ctl("plugin", "msg", "done", "done")
        assert result.returncode == 0, result.output
        wait_for_metric(self._ats, "txn_type_verify.test.done", 1)
        wait_for_metric(self._ats, "txn_type_verify.error", 0)
        wait_for_metric(self._ats, "txn_type_verify.tunnel.start", 1)
        wait_for_metric(self._ats, "txn_type_verify.http.req", 2)


def test_txn_type(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Plugins distinguish HTTP transactions, SNI tunnels, and CONNECT."""

    TransactionTypeScenario(ats_factory, services, curl).run()
