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
"""Verify that an idle HTTP/3 request stream does not keep waking up to write."""

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent

H3_IDLE_CLIENT__replay = "replays/h3_server_for_idle_client.replay.yaml"

# How long the client holds its request stream open with nothing to send. Long
# enough that a connection which polls for writes every few milliseconds logs
# far more stream WRITE_READY events than the requests themselves produce.
IDLE_SECONDS = 2

# The requests themselves produce a few dozen WRITE_READY events. Polling while
# the stream is idle produces hundreds per second.
MAX_WRITE_READY_EVENTS = 100


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin, which waits out the idle period for the POST body.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("server-h3-idle", H3_IDLE_CLIENT__replay, verbose=False, other_args="--poll-timeout 30000")


def configure_ats(ats_factory: ATSFactory, *, server: VerifierServer) -> ATS:
    """Configure HTTP/3 with the Http3App event debug output that the test counts.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param server: Origin that ATS forwards the requests to.
    """

    ats = ats_factory.create("ts-h3-idle", enable_tls=True, enable_quic=True, enable_cache=False)
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
            # v_http3 logs every stream event that Http3App dispatches.
            "proxy.config.diags.debug.tags": "v_http3",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            # Keep the connection open through the idle period.
            "proxy.config.quic.no_activity_timeout_in": 30000,
        }
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}")
    return ats


def configure_client(services: ServiceFactory, *, ats: ATS) -> ProcessService:
    """Create the aioquic client that holds a request stream open and idle.

    :param services: Factory owning support services and their cleanup.
    :param ats: Traffic Server instance the client connects to.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "py_h3_client" / "h3_idle_client.py",
            "--addr",
            f"127.0.0.1:{ats.https_port}",
            "--authority",
            f"py.example.com:{ats.https_port}",
            "--server-name",
            "py.example.com",
            "--idle-seconds",
            str(IDLE_SECONDS),
        ),
    )


def test_h3_idle_write_ready(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """An open but idle HTTP/3 request stream is not signaled repeatedly, and still completes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not ats_factory.has_feature("TS_USE_QUIC"):
        pytest.skip("Traffic Server was built without QUIC support")
    server = configure_server(services)
    ats = configure_ats(ats_factory, server=server)
    client = configure_client(services, ats=ats)

    server.start()
    ats.start()
    result = client.run(timeout=60)
    assert result.returncode == 0, result.output
    assert "completed idle HTTP/3 checks" in result.stdout

    diags = ats.diags_log.read_text(errors="replace") + ats.traffic_out.read_text(errors="replace")
    write_ready_events = sum(1 for line in diags.splitlines() if "VC_EVENT_WRITE_READY" in line)
    assert write_ready_events > 0, "v_http3 debug output was not captured"
    assert write_ready_events <= MAX_WRITE_READY_EVENTS, (
        f"{write_ready_events} stream WRITE_READY events with a request stream idle for {IDLE_SECONDS}s; "
        "the QUIC connection is polling for writes while idle"
    )
