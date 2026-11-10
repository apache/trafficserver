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
import time
from pathlib import Path

import pytest
from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

NET_THREADS = 4
# Each connection comes from its own UDP port, and the kernel picks a listener socket from a hash
# of the addresses and ports. The chance that all of these connections hash to the same one of
# NET_THREADS sockets is 4^-31, about one in 5 x 10^18.
CONNECTIONS = 32
REQUESTS_PER_CONNECTION = 4
RESPONSE_SIZE = 16
# What h3_client.generated_body(RESPONSE_SIZE) returns, which the client checks every body against.
BODY = "0000000 0000001 "

ACCEPTED_RE = r"\[(ET_NET \d+)\] DIAG: .*\(http3\) \[[^]]*\] accepted connection from \S+"
LISTENERS_STARTED = f"accepting OpenSSL QUIC connections on {NET_THREADS} of {NET_THREADS} net threads"
LISTENER_FAILED_RE = r"failed to .*OpenSSL QUIC"


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


def configure_ats(ats_factory: ATSFactory, name: str, records: dict[str, int], *, _origin: OriginServer) -> ATS:
    """Configure a fixed number of net threads and log which one accepts each connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Name of this Traffic Server instance.
    :param records: QUIC records to set on top of the defaults.
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
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": NET_THREADS,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http3|openssl_quic",
            **records,
        }
    )
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def start_ats(ats_factory: ATSFactory, services: ServiceFactory, name: str, records: dict[str, int]) -> ATS:
    """Start the origin and a Traffic Server instance in front of it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param name: Name of this Traffic Server instance.
    :param records: QUIC records to set on top of the defaults.
    """

    if not ats_factory.has_feature("TS_HAS_OPENSSL_QUIC"):
        pytest.skip("Only the OpenSSL QUIC implementation has per-thread listeners")
    if sys.platform not in ("linux", "freebsd"):
        pytest.skip("Only Linux and FreeBSD spread a shared UDP port over its sockets")
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, name, records, _origin=_origin)
    _origin.start()
    _ats.start()
    return _ats


def run_connections(services: ServiceFactory, name: str, retried: int, *, _ats: ATS) -> None:
    """Open every connection at once from an aioquic client and serve every request on each.

    :param services: Factory owning support services and their cleanup.
    :param name: Name of this client process.
    :param retried: Number of connections the server should make retry.
    :param _ats: Traffic Server instance
    """

    client = services.process(
        name,
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
            "--response-size",
            str(RESPONSE_SIZE),
            "--connections",
            str(CONNECTIONS),
            "--requests",
            str(REQUESTS_PER_CONNECTION),
        ),
    )
    result = client.run(timeout=120)
    assert result.returncode == 0, result.output
    assert f"completed {REQUESTS_PER_CONNECTION} requests on each of {CONNECTIONS} HTTP/3 connections" in result.stdout
    assert f"{retried} connections were retried" in result.stdout, result.stdout


def accepting_threads(connections: int, *, _ats: ATS) -> set[str]:
    """Return the net threads that accepted the connections.

    :param connections: Number of connections Traffic Server has accepted so far.
    :param _ats: Traffic Server instance
    """

    accepted = re.findall(ACCEPTED_RE, wait_for_file_lines(_ats.traffic_out, ACCEPTED_RE, connections))
    assert len(accepted) == connections, accepted
    threads = set(accepted)
    assert threads <= {f"ET_NET {i}" for i in range(NET_THREADS)}, threads
    assert not re.search(LISTENER_FAILED_RE, _ats.diags_log.read_text(errors="replace"))
    return threads


def test_h3_connections_spread_over_net_threads(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """With the default configuration, HTTP/3 connections are accepted on more than one net thread.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    _ats = start_ats(ats_factory, services, "ts-h3-listener-threads", {})
    run_connections(services, "client", 0, _ats=_ats)
    threads = accepting_threads(CONNECTIONS, _ats=_ats)
    assert len(threads) > 1, threads
    assert LISTENERS_STARTED in _ats.traffic_out.read_text(errors="replace")

    # Let each listener's closed-connection collector, which runs every 100 ms, free the first
    # round's connections before the second round opens new ones.
    time.sleep(0.5)
    run_connections(services, "client-again", 0, _ats=_ats)
    accepting_threads(2 * CONNECTIONS, _ats=_ats)
    assert _ats.is_running


def test_h3_single_listener_uses_one_thread(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """With per-thread listeners turned off, one net thread accepts every connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    _ats = start_ats(ats_factory, services, "ts-h3-single-listener", {"proxy.config.quic.server.listen_per_thread": 0})
    run_connections(services, "client", 0, _ats=_ats)
    threads = accepting_threads(CONNECTIONS, _ats=_ats)
    assert len(threads) == 1, threads
    assert "accepting OpenSSL QUIC connections on" not in _ats.traffic_out.read_text(errors="replace")


@pytest.mark.parametrize("stateless_retry", [0, 1])
def test_h3_stateless_retry_with_listener_threads(ats_factory: ATSFactory, services: ServiceFactory, stateless_retry: int) -> None:
    """Connections complete with and without a stateless retry when each net thread has its own listener.

    Each OpenSSL listener encrypts its retry tokens with its own key, so a token only validates on
    the listener that issued it. A client sends its second Initial from the same address and port,
    which reach the same socket.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param stateless_retry: Value for proxy.config.quic.server.stateless_retry_enabled.
    """

    _ats = start_ats(
        ats_factory,
        services,
        f"ts-h3-listener-threads-retry-{stateless_retry}",
        {"proxy.config.quic.server.stateless_retry_enabled": stateless_retry},
    )
    run_connections(services, "client", CONNECTIONS if stateless_retry else 0, _ats=_ats)
    threads = accepting_threads(CONNECTIONS, _ats=_ats)
    assert len(threads) > 1, threads
