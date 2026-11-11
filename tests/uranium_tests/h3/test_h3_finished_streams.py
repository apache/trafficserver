#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not use
#  this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Finished HTTP/3 request streams release their OpenSSL stream objects before the connection is freed."""

from pathlib import Path
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

H3_PYTHON_CLIENT__replay = "replays/h3_server_for_python_client.replay.yaml"

SEQUENTIAL_REQUESTS = 40

# Four streams live as long as the connection: ATS's control stream and the
# client's control, QPACK encoder and QPACK decoder streams. The fifth allows
# for the last request stream, which the application may delete after the
# final pass that would release it.
MAX_STREAMS_HELD_AT_CLOSE = 5

# The free normally follows the client's CONNECTION_CLOSE at once. If that
# packet is lost, the connection is freed only after the 30 second
# no-activity timeout.
FREE_CONNECTION_TIMEOUT = 45

FREE_CONNECTION_LINE = r"Free connection, ([0-9]+) stream objects still held"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin that answers the repeated small GET.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server(
        "server-h3-finished-streams", H3_PYTHON_CLIENT__replay, verbose=False, other_args="--poll-timeout 30000"
    )


def configure_ats(ats_factory: ATSFactory, *, _server: VerifierServer) -> ATS:
    """Configure HTTP/3 with quic_net debugging on, so the stream count logged when a connection is freed reaches traffic.out.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-h3-finished-streams", enable_tls=True, enable_quic=True, enable_cache=False)
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic_net",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
        }
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the aioquic client that sends the requests on one connection.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "py_h3_client" / "h3_client.py",
            "--addr",
            f"127.0.0.1:{_ats.https_port}",
            "--authority",
            f"py.example.com:{_ats.https_port}",
            "--server-name",
            "py.example.com",
            "--sequential-requests",
            str(SEQUENTIAL_REQUESTS),
        ),
    )


def test_h3_finished_streams_released(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A connection that served many requests holds little more than its long-lived streams when it is freed.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not (ats_factory.has_feature("TS_USE_QUIC") and ats_factory.has_feature("TS_HAS_OPENSSL_QUIC")):
        pytest.skip("ATS was built without the OpenSSL QUIC backend")
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _server=_server)
    _client = configure_client(services, _ats=_ats)

    _server.start()
    _ats.start()
    result = _client.run(timeout=120)
    assert result.returncode == 0, result.output
    assert f"completed {SEQUENTIAL_REQUESTS} sequential HTTP/3 requests" in result.stdout

    content = wait_for_file_lines(_ats.traffic_out, FREE_CONNECTION_LINE, 1, timeout=FREE_CONNECTION_TIMEOUT)
    most_held = max(int(count) for count in re.findall(FREE_CONNECTION_LINE, content))
    assert most_held <= MAX_STREAMS_HELD_AT_CLOSE, (
        f"a connection that served {SEQUENTIAL_REQUESTS} requests still held {most_held} stream objects "
        f"when it was freed; expected at most {MAX_STREAMS_HELD_AT_CLOSE}"
    )
