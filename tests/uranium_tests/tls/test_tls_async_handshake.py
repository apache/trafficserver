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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProceduralContext, ServiceFactory, wait_for_file_lines


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve the request made after the asynchronous handshake.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
            "body": ""
        },
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
    """Load the async hook and enable OpenSSL asynchronous handshakes.

    :param _origin: Test-local origin configured by the test.
    :param _plugin: Test-local plugin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_custom_plugin(_plugin)
    ats.plugin_config.add_line(_plugin.name)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.async.handshake.enabled": 1,
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "ssl|http",
        })
    return ats


def test_tls_async_handshake(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    procedural_context: ProceduralContext,
    curl: Curl,
) -> None:
    """OpenSSL asynchronous TLS handshakes resume and complete.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param procedural_context: Procedural context used by this test step.
    :param curl: Transport-aware curl command runner.
    """

    version = subprocess.run(("openssl", "version"), capture_output=True, text=True, check=False).stdout
    if "BoringSSL" in version:
        pytest.skip("the async job interface requires OpenSSL")
    context = procedural_context
    _plugin = context.runtime.resolve_artifact(context.test_directory, "{AtsTestPluginsDir}/async_handshake.so")
    if not _plugin.is_file():
        pytest.skip(f"async handshake test plugin is absent: {_plugin}")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin, _plugin=_plugin)

    _origin.start()
    _ats.start()
    result = curl.run(
        (f"--insecure --verbose --header 'uuid: basic' --header 'Host: example.com' "
         f"'https://127.0.0.1:{_ats.https_port}/'"),)
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200" in result.output or "HTTP/2 200" in result.output
    wait_for_file_lines(_ats.traffic_out, "resumed OpenSSL async job", 1)
