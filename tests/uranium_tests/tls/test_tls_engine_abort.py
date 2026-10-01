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
import subprocess
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the normal response used after the abort barrage.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n"},
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                    "Cache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n"),
            "body": "ok",
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer, _plugin: Path) -> ATS:
    """Enable asynchronous TLS handshakes with a two-second pause.

    :param _origin: Test-local origin configured by the test.
    :param _plugin: Test-local plugin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.async.handshake.enabled": 1,
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "ssl",
        })
    ats.copy_custom_plugin(_plugin)
    ats.plugin_config.add_line("async_handshake.so -delay-ms=2000")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the client that abandons thirty in-flight handshakes.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "abort-client",
        (sys.executable, TEST_DIRECTORY / "tls_engine_abort.py", str(_ats.https_port), "30"),
    )


def test_tls_engine_abort(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Aborted asynchronous handshakes leave no stale poller registration.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if curl.uses_uds:
        pytest.skip("the TLS abort client requires a TCP listener")
    openssl = subprocess.check_output(("openssl", "version"), text=True)
    version = re.search(r"\d+(?:\.\d+)+", openssl)
    if not openssl.startswith("OpenSSL") or version is None:
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    if tuple(int(part) for part in version.group().split(".")) < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    _plugin = services.resolve_path("{AtsTestPluginsDir}/async_handshake.so")
    if not _plugin.is_file():
        pytest.skip(f"{_plugin} not found")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin, _plugin=_plugin)
    _abort_client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    aborts = _abort_client.run(timeout=30)
    assert aborts.returncode == 0, aborts.output
    assert "sent 30 aborted handshakes" in aborts.output

    result = curl.run_for(
        _ats,
        (f"--insecure --verbose --header uuid:basic --header host:example.com "
         f"'https://127.0.0.1:{_ats.https_port}/'"),
        timeout=15,
    )
    assert result.returncode == 0, result.output
    assert re.search(r"HTTP/(2|1\.1) 200", result.output), result.output
    traffic_out = wait_for_file_lines(_ats.traffic_out, "sent async wake signal to", 1, timeout=10)
    assert "AddressSanitizer" not in traffic_out
