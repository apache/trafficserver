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

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TLS_FLOW_CONTROL__low_water = 32 * 1024
TLS_FLOW_CONTROL__high_water = 64 * 1024

TLS_FLOW_CONTROL__body_length = 8 * 1024 * 1024


def configure_server(services: ServiceFactory) -> OriginServer:
    """Create an origin response larger than the configured watermarks.

    :param services: Factory owning support services and their cleanup.
    """

    server = services.origin("server")
    server.add_response(
        {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n"},
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                    f"Content-Length: {TLS_FLOW_CONTROL__body_length}\r\n\r\n"),
            "body": "x" * TLS_FLOW_CONTROL__body_length,
        },
    )
    return server


def configure_ats(ats_factory: ATSFactory, *, _server: OriginServer) -> ATS:
    """Enable small HTTP tunnel flow-control watermarks over TLS.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.http.flow_control.enabled": 1,
            "proxy.config.http.flow_control.high_water": TLS_FLOW_CONTROL__high_water,
            "proxy.config.http.flow_control.low_water": TLS_FLOW_CONTROL__low_water,
        })
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the deliberately slow TLS response reader.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "tls_flow_control_client.py",
            "-p",
            str(_ats.https_port),
            "--host",
            "ex.test",
            "--path",
            "/obj",
            "--expect-bytes",
            str(TLS_FLOW_CONTROL__body_length),
        ),
    )


def test_tls_flow_control(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """TLS tunnel flow control throttles safely without stalling delivery.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _server=_server)
    _client = configure_client(services, _ats=_ats)

    _server.start()
    _ats.start()
    result = _client.run(timeout=60)
    assert result.returncode == 0, result.output
    assert "RESULT=PASS" in result.output
    assert f"BODY_BYTES={TLS_FLOW_CONTROL__body_length}" in result.output
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    for expression in ("received signal", "failed assertion", "AddressSanitizer", "use-after-free", "runtime error:"):
        assert expression not in traffic_out
