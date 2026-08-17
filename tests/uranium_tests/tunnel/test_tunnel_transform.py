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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, wait_for_metric

TEST_DIRECTORY = Path(__file__).parent


class TunnelTransformScenario:
    """Compare a tunnel transform's byte metrics with an external observer."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("Tunnel byte accounting requires a TCP client connection")
        self._services = services
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        self._proxy = self.configure_proxy(services)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the TLS origin behind the blind tunnel."""

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the SNI tunnel and byte-counting transform plugin."""

        ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.pem")
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.key")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/tunnel_transform.so")
        ats.plugin_config.add_line("tunnel_transform.so")
        ats.records.update(
            {
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
        ats.write_config_file(
            "sni.yaml",
            "sni:\n"
            "  - fqdn: tunnel-test\n"
            f"    tunnel_route: localhost:{self._origin.https_port}\n",
        )
        ats.allow_private_connect()
        return ats

    def configure_proxy(self, services: ServiceFactory) -> ProcessService:
        """Create a byte-counting TCP forwarder in front of ATS."""

        port = services.allocate_port()
        self._proxy_port = port
        return services.process(
            "dumb-proxy",
            (
                sys.executable,
                TEST_DIRECTORY / "dumb_proxy.py",
                "--listening_port",
                str(port),
                "--forwarding_port",
                str(self._ats.https_port),
            ),
            ready_port=port,
        )

    @staticmethod
    def observed_bytes(output: str, key: str) -> int:
        """Extract one direction's byte count from the proxy transcript."""

        match = re.search(rf"{re.escape(key)}:\s+(\d+)", output)
        assert match is not None, output
        return int(match.group(1))

    @staticmethod
    def metric(ats: ATS, name: str) -> int:
        """Read one integer ATS metric."""

        result = ats.traffic_ctl("metric", "get", name)
        assert result.returncode == 0, result.output
        return int(result.stdout.split()[-1])

    def run(self) -> None:
        """Drive one tunnel and compare plugin and wire-observer byte counts."""

        self._origin.start()
        self._ats.start()
        self._proxy.start()
        result = self._curl.run_for(
            self._ats,
            (
                f"--insecure --http1.1 --header 'Connection: close' --verbose --silent --resolve "
                f"'tunnel-test:{self._proxy_port}:127.0.0.1' 'https://tunnel-test:{self._proxy_port}/'"),
        )
        assert result.returncode == 0, result.output
        proxy_result = self._proxy.wait(timeout=10)

        done = self._ats.traffic_ctl("plugin", "msg", "done", "done")
        assert done.returncode == 0, done.output
        wait_for_metric(self._ats, "tunnel_transform.test.done", 1)
        wait_for_metric(self._ats, "tunnel_transform.error", 0)
        assert self.metric(self._ats,
                           "tunnel_transform.ua.bytes_sent") == self.observed_bytes(proxy_result.output, "client-to-server")
        assert self.metric(self._ats,
                           "tunnel_transform.os.bytes_sent") == self.observed_bytes(proxy_result.output, "server-to-client")


def test_tunnel_transform(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Tunnel transforms report the exact encrypted byte counts on the wire."""

    TunnelTransformScenario(ats_factory, services, curl).run()
