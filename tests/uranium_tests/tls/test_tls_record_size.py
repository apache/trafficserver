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

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory, *, _body_length: int) -> OriginServer:
    """Create a response large enough to exercise the selected strategy.

    :param _body_length: Test-local body length configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n"},
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                    f"Cache-Control: max-age=3600\r\nContent-Length: {_body_length}\r\n\r\n"),
            "body": "x" * _body_length,
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _max_record: int, _origin: OriginServer) -> ATS:
    """Configure the requested TLS record-size strategy.

    :param _max_record: Test-local max record configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.records.update({"proxy.config.ssl.max_record_size": _max_record})
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _body_length: int, _max_record: int) -> ProcessService:
    """Create the client that inspects TLS records on the wire.

    :param _ats: Test-local ats configured by the test.
    :param _body_length: Test-local body length configured by the test.
    :param _max_record: Test-local max record configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    option = ("--dynamic",) if _max_record == -1 else ("--max-record", str(_max_record))
    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "tls_record_size_client.py",
            "-p",
            str(_ats.https_port),
            "--host",
            "ex.test",
            "--path",
            "/obj",
            *option,
            "--expect-bytes",
            str(_body_length),
        ),
    )


@pytest.mark.parametrize(("max_record", "body_length"), ((4096, 1024 * 1024), (-1, 2 * 1024 * 1024)))
def test_tls_record_size(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    max_record: int,
    body_length: int,
) -> None:
    """TLS records follow the configured fixed or dynamic sizing strategy.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param max_record: Maximum TLS record size in bytes.
    :param body_length: Response body length in bytes.
    """
    _origin = configure_origin(services, _body_length=body_length)
    _ats = configure_ats(ats_factory, _max_record=max_record, _origin=_origin)
    _client = configure_client(services, _ats=_ats, _body_length=body_length, _max_record=max_record)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=60)
    assert result.returncode == 0, result.output
    if max_record == -1:
        assert "PASS: TLS records ramp from small to large after the dynamic threshold" in result.output
        traffic_out = _ats.traffic_out.read_text(errors="replace")
        assert "proxy.config.ssl.max_record_size" not in traffic_out or "Validity Check error" not in traffic_out
    else:
        assert "PASS: every application-data record is within the configured clamp" in result.output
