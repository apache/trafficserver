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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory, *, _origin_port: int) -> ProcessService:
    """Create the raw TLS origin that sends an RST mid-body.

    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    certificate = TEST_DIRECTORY.parents[1] / "tools" / "ssl" / "server.pem"
    origin = services.process(
        "origin",
        (
            sys.executable,
            TEST_DIRECTORY / "tls_post_abort_origin.py",
            "-p",
            str(_origin_port),
            "-c",
            certificate,
            "-d",
            "1.0",
        ),
        ready_port=_origin_port,
    )
    origin.stdout.contains(
        "request headers received",
        "The origin should receive the POST headers before resetting the connection.",
    )
    origin.stdout.contains(
        "connection reset sent",
        "The origin should reset the connection while ATS sends the request body.",
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Configure a TLS origin with timeouts much longer than the test limit.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.url_remap.remap_required": 1,
            "proxy.config.http.connect_attempts_max_retries": 0,
            "proxy.config.http.connect_attempts_timeout": 15,
            "proxy.config.http.transaction_no_activity_timeout_out": 15,
            "proxy.config.http.transaction_no_activity_timeout_in": 30,
            "proxy.config.net.sock_send_buffer_size_out": 65536,
            "proxy.config.ssl.client.verify.server.policy": "DISABLED",
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http|ssl|ssl_io",
        })
    ats.remap_config.add_line(f"map /post https://127.0.0.1:{_origin_port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the timed streaming POST client.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "tls_post_abort_client.py",
            "-p",
            str(_ats.http_port),
            "-t",
            "8",
        ),
    )


def test_tls_origin_post_abort(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A TLS origin RST fails an in-flight POST promptly without crashing ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _origin = configure_origin(services, _origin_port=_origin_port)
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    client = _client.run(timeout=12)
    assert client.returncode == 0, client.output
    assert "PASS: transaction failed promptly" in client.output
    assert "status-code: 5" in client.output
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    assert "received signal" not in traffic_out
    assert "failed assertion" not in traffic_out
