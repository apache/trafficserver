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


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the TLS origin used by HTTP, SNI tunnel, and CONNECT traffic.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin", ssl=True)
    response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"}
    origin.add_response({"headers": "GET / HTTP/1.1\r\nHost: http-test\r\n\r\n"}, response)
    origin.add_response({"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n"}, response)
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure the test plugin and three TLS routing paths.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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
            "proxy.config.http.connect_ports": str(_origin.https_port),
        })
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.remap_config.add_line(f"map https://http-test:{ats.https_port}/ https://127.0.0.1:{_origin.https_port}/")
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: tunnel-test\n"
        f"    tunnel_route: localhost:{_origin.https_port}\n",
    )
    ats.allow_private_connect(("CONNECT", "GET"))
    return ats


def request(*arguments: str, _ats: ATS, _curl: Curl) -> None:
    """Run a curl request and require a successful exchange.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output


def send_traffic(*, _ats: ATS, _curl: Curl) -> None:
    """Send ordinary HTTP, SNI tunnel, and CONNECT transactions.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    common = ("--insecure", "--http1.1", "--header", "Connection: close", "--verbose", "--silent")
    request(
        *common,
        "--resolve",
        f"http-test:{_ats.https_port}:127.0.0.1",
        f"https://http-test:{_ats.https_port}/",
        _ats=_ats,
        _curl=_curl)
    request(
        *common,
        "--resolve",
        f"tunnel-test:{_ats.https_port}:127.0.0.1",
        f"https://tunnel-test:{_ats.https_port}/",
        _ats=_ats,
        _curl=_curl)
    request(
        *common,
        "--resolve",
        f"connect-proxy:{_ats.http_port}:127.0.0.1",
        "--proxy",
        f"http://connect-proxy:{_ats.http_port}",
        "--resolve",
        f"http-test:{_ats.https_port}:127.0.0.1",
        f"https://http-test:{_ats.https_port}/",
        _ats=_ats,
        _curl=_curl)


def test_txn_type(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Plugins distinguish HTTP transactions, SNI tunnels, and CONNECT.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("Transaction type coverage requires TCP client connections")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    send_traffic(_ats=_ats, _curl=curl)
    result = _ats.traffic_ctl("plugin", "msg", "done", "done")
    assert result.returncode == 0, result.output
    wait_for_metric(_ats, "txn_type_verify.test.done", 1)
    wait_for_metric(_ats, "txn_type_verify.error", 0)
    wait_for_metric(_ats, "txn_type_verify.tunnel.start", 1)
    wait_for_metric(_ats, "txn_type_verify.http.req", 2)
