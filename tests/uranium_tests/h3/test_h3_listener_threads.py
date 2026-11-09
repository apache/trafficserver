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

import re
import sys
from pathlib import Path

import pytest
from tools.uranium.services import ATS, ATSFactory, OriginServer, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

NET_THREADS = 4
# Each connection comes from its own UDP port, and the kernel picks a listener socket from a hash
# of the addresses and ports. With this many connections over NET_THREADS sockets, the chance that
# every connection hashes to the same socket is about one in 10^18.
CONNECTIONS = 32
REQUESTS_PER_CONNECTION = 4
BODY = "per-thread"

ACCEPTED_RE = re.compile(r"\[(ET_NET \d+)\] DIAG: .*\(http3\) \[[^]]*\] accepted connection from (\S+)")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve one small object for every request.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /listener-threads HTTP/1.1\r\nHost: py.example.com\r\n\r\n", "body": ""},
        {"headers": f"HTTP/1.1 200 OK\r\nContent-Length: {len(BODY)}\r\n\r\n", "body": BODY},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, name: str, per_thread: int | None, *, _origin: OriginServer) -> ATS:
    """Configure a fixed number of net threads and log which one accepts each connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Name of this Traffic Server instance.
    :param per_thread: Value for proxy.config.quic.server.listen_per_thread, or None to leave the default.
    :param _origin: Test-local origin configured by the test.
    """

    ats = ats_factory.create(name, enable_tls=True, enable_quic=True, enable_cache=False)
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
    records = {
        "proxy.config.exec_thread.autoconfig.enabled": 0,
        "proxy.config.exec_thread.limit": NET_THREADS,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http3",
        "proxy.config.quic.server.stateless_retry_enabled": 0,
    }
    if per_thread is not None:
        records["proxy.config.quic.server.listen_per_thread"] = per_thread
    ats.records.update(records)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.set_logging_yaml(
        {
            "logging": {
                "formats": [{"name": "h3_threads_access", "format": "client_version=%<cqpv> status=%<pssc> c_url=%<cquuc>"}],
                "logs": [{"filename": "h3_threads_access", "format": "h3_threads_access"}],
            }
        }
    )
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create an aioquic client that opens every connection at once.

    :param services: Factory owning support services and their cleanup.
    :param _ats: Test-local ats configured by the test.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "py_h3_client" / "h3_many_connections.py",
            "--addr",
            f"127.0.0.1:{_ats.https_port}",
            "--authority",
            f"py.example.com:{_ats.https_port}",
            "--server-name",
            "py.example.com",
            "--path",
            "/listener-threads",
            "--body",
            BODY,
            "--connections",
            str(CONNECTIONS),
            "--requests",
            str(REQUESTS_PER_CONNECTION),
        ),
    )


def run_connections(ats_factory: ATSFactory, services: ServiceFactory, name: str, per_thread: int | None) -> set[str]:
    """Serve every request on every connection and return the threads that accepted them.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param name: Name of this Traffic Server instance.
    :param per_thread: Value for proxy.config.quic.server.listen_per_thread, or None to leave the default.
    """

    if not ats_factory.has_feature("TS_HAS_OPENSSL_QUIC"):
        pytest.skip("Only the OpenSSL QUIC implementation has per-thread listeners")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, name, per_thread, _origin=_origin)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=120)
    assert result.returncode == 0, result.output
    assert f"completed {REQUESTS_PER_CONNECTION} requests on each of {CONNECTIONS} HTTP/3 connections" in result.stdout

    wait_for_file_lines(
        _ats.log_directory / "h3_threads_access.log",
        r"client_version=http/3 status=200 c_url=https://py\.example\.com:[0-9]+/listener-threads",
        CONNECTIONS * REQUESTS_PER_CONNECTION,
    )

    accepted = ACCEPTED_RE.findall(_ats.traffic_out.read_text(errors="replace"))
    peers = {peer for _, peer in accepted}
    assert len(peers) == CONNECTIONS, accepted
    return {thread for thread, _ in accepted}


def test_h3_connections_spread_over_net_threads(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HTTP/3 connections are accepted and served on more than one net thread.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    threads = run_connections(ats_factory, services, "ts-h3-listener-threads", None)
    assert len(threads) > 1, threads


def test_h3_single_listener_uses_one_thread(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """With per-thread listeners turned off, one net thread accepts every connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    threads = run_connections(ats_factory, services, "ts-h3-single-listener", 0)
    assert len(threads) == 1, threads
