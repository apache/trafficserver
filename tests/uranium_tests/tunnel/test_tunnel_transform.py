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


def test_tunnel_transform(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Tunnel transforms report the exact encrypted byte counts on the wire.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _proxy_port: int

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the TLS origin behind the blind tunnel.

        :param services: Factory owning support services and their cleanup.
        """

        origin = services.origin("origin", ssl=True)
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
        )
        return origin

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure the SNI tunnel and byte-counting transform plugin.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.pem")
        ats.copy_to_ssl(TEST_DIRECTORY.parent / "tls" / "ssl" / "server.key")
        ats.copy_custom_plugin("{AtsTestPluginsDir}/tunnel_transform.so")
        ats.plugin_config.add_line("tunnel_transform.so")
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.http.connect_ports": str(_origin.https_port),
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
            f"    tunnel_route: localhost:{_origin.https_port}\n",
        )
        ats.allow_private_connect()
        return ats

    def configure_proxy(services: ServiceFactory) -> ProcessService:
        """Create a byte-counting TCP forwarder in front of ATS.

        :param services: Factory owning support services and their cleanup.
        """
        nonlocal _proxy_port

        port = services.allocate_port()
        _proxy_port = port
        return services.process(
            "dumb-proxy",
            (
                sys.executable,
                TEST_DIRECTORY / "dumb_proxy.py",
                "--listening_port",
                str(port),
                "--forwarding_port",
                str(_ats.https_port),
            ),
            ready_port=port,
        )

    def observed_bytes(output: str, key: str) -> int:
        """Extract one direction's byte count from the proxy transcript.

        :param output: Output used by this test step.
        :param key: Key used by this test step.
        """

        match = re.search(rf"{re.escape(key)}:\s+(\d+)", output)
        assert match is not None, output
        return int(match.group(1))

    def metric(ats: ATS, name: str) -> int:
        """Read one integer ATS metric.

        :param ats: Traffic Server instance configured or queried by this step.
        :param name: Unique service or case name within this test.
        """

        result = ats.traffic_ctl("metric", "get", name)
        assert result.returncode == 0, result.output
        return int(result.stdout.split()[-1])

    if curl.uses_uds:
        pytest.skip("Tunnel byte accounting requires a TCP client connection")
    _services = services
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)
    _proxy = configure_proxy(services)

    _origin.start()
    _ats.start()
    _proxy.start()
    result = curl.run_for(
        _ats,
        (
            f"--insecure --http1.1 --header 'Connection: close' --verbose --silent --resolve "
            f"'tunnel-test:{_proxy_port}:127.0.0.1' 'https://tunnel-test:{_proxy_port}/'"),
    )
    assert result.returncode == 0, result.output
    proxy_result = _proxy.wait(timeout=10)

    done = _ats.traffic_ctl("plugin", "msg", "done", "done")
    assert done.returncode == 0, done.output
    wait_for_metric(_ats, "tunnel_transform.test.done", 1)
    wait_for_metric(_ats, "tunnel_transform.error", 0)
    assert metric(_ats, "tunnel_transform.ua.bytes_sent") == observed_bytes(proxy_result.output, "client-to-server")
    assert metric(_ats, "tunnel_transform.os.bytes_sent") == observed_bytes(proxy_result.output, "server-to-client")
