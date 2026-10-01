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
"""Verify stek_share distributes TLS session-ticket keys across ATS nodes."""

from pathlib import Path
import re
import shlex
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent

STEK_SHARE_CIPHER_SUITE = (
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:"
    "DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:"
    "ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:"
    "ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:"
    "DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:"
    "DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:"
    "DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:"
    "DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:"
    "!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:"
    "!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA")
STEK_SHARE__FOLLOWER_STEK = re.compile(
    r"Received new STEK: ([0-9A-F]+).*?Update SSL Ticket Key succeeded\.",
    re.DOTALL,
)

STEK_SHARE__LEADER_STEK = re.compile(r"Using (?:initial|new) STEK: ([0-9A-F]+)")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the HTTP origin behind every TLS node.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "curl test"
        },
    )
    return origin


def server_list(*, _cluster_ports: list[int]) -> str:
    """Render the five dynamically allocated cluster endpoints.

    :param _cluster_ports: Test-local cluster ports configured by the test.
    """

    lines = []
    for server_id, port in enumerate(_cluster_ports, start=1):
        lines.extend((
            f"- server_id: {server_id}",
            "  address: 127.0.0.1",
            f"  port: {port}",
        ))
    return "\n".join(lines) + "\n"


def plugin_config(ats: ATS, server_id: int, *, _cluster_ports: list[int]) -> str:
    """Render one node's Raft and certificate configuration.

    :param _cluster_ports: Test-local cluster ports configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param server_id: Server id used by this test step.
    """

    return "\n".join(
        (
            f"server_id: {server_id}",
            "address: 127.0.0.1",
            f"port: {_cluster_ports[server_id - 1]}",
            "asio_thread_pool_size: 4",
            "heart_beat_interval: 100",
            "election_timeout_lower_bound: 200",
            "election_timeout_upper_bound: 400",
            "reserved_log_items: 5",
            "snapshot_distance: 5",
            "client_req_timeout: 3000",
            "key_update_interval: 3600",
            f"server_list_file: {ats.config_directory / 'server_list.yaml'}",
            f"root_cert_file: {ats.ssl_directory / 'self_signed.crt'}",
            f"server_cert_file: {ats.ssl_directory / 'self_signed.crt'}",
            f"server_key_file: {ats.ssl_directory / 'self_signed.key'}",
            "cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share",
        )) + "\n"


def configure_ats(ats_factory: ATSFactory, index: int, *, _cluster_ports: list[int], _origin: OriginServer) -> ATS:
    """Configure one TLS endpoint and stek_share cluster member.

    :param _cluster_ports: Test-local cluster ports configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param index: Index used by this test step.
    """

    server_id = index + 1
    ats = ats_factory.create(f"ats-{server_id}", enable_tls=True)
    if not ats.plugin_exists("stek_share.so"):
        pytest.skip("stek_share.so is not installed")
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "self_signed.crt",
        TEST_DIRECTORY / "ssl" / "self_signed.key",
    )
    ats.set_ssl_multicert_yaml(
        {"ssl_multicert": [{
            "dest_ip": "*",
            "ssl_cert_name": "self_signed.crt",
            "ssl_key_name": "self_signed.key",
        }]})
    ats.write_config_file("server_list.yaml", server_list(_cluster_ports=_cluster_ports))
    ats.write_config_file("stek_share_conf.yaml", plugin_config(ats, server_id, _cluster_ports=_cluster_ports))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "stek_share",
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.cipher_suite": STEK_SHARE_CIPHER_SUITE,
        })
    ats.plugin_config.add_line(f"stek_share.so {ats.config_directory / 'stek_share_conf.yaml'}")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def active_stek(content: str) -> str | None:
    """Return the latest cluster key that a node has activated.

    :param content: Complete stek_share diagnostic output for one node.
    """

    events = [(match.start(), match.group(1)) for match in STEK_SHARE__LEADER_STEK.finditer(content)]
    events.extend((match.start(), match.group(1)) for match in STEK_SHARE__FOLLOWER_STEK.finditer(content))
    return max(events)[1] if events else None


def wait_for_cluster(timeout: float = 30, *, _ats_nodes: list[ATS]) -> None:
    """Wait until every node has activated the same shared key.

    :param timeout: Maximum number of seconds to wait for cluster convergence.

    :param _ats_nodes: Test-local ats nodes configured by the test.
    """

    deadline = time.monotonic() + timeout
    observed: dict[str, str | None] = {}
    while time.monotonic() < deadline:
        for ats in _ats_nodes:
            content = ats.traffic_out.read_text(errors="replace") if ats.traffic_out.exists() else ""
            observed[ats.name] = active_stek(content)
        active_keys = list(observed.values())
        if all(active_keys) and len(set(active_keys)) == 1:
            return
        time.sleep(0.1)
    raise AssertionError(f"STEK cluster did not converge within {timeout} seconds: {observed}")


def verify_basic_request(*, _ats_nodes: list[ATS], _curl: Curl) -> None:
    """Confirm the first TLS endpoint still proxies ordinary requests.

    :param _ats_nodes: Test-local ats nodes configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    ats = _ats_nodes[0]
    result = _curl.run_for(
        ats,
        (f"--insecure --silent --show-error --header 'Host: www.example.com' "
         f"'https://127.0.0.1:{ats.https_port}/'"),
    )
    assert result.returncode == 0, result.output
    assert "curl test" in result.stdout


def openssl_handshake(ats: ATS, *, save: bool, _session_file: Path) -> str:
    """Create or resume a TLS 1.2 session and return OpenSSL diagnostics.

    :param _session_file: Test-local session file configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param save: Save used by this test step.
    """

    session_option = "-sess_out" if save else "-sess_in"
    request = "GET / HTTP/1.1\\r\\nHost: www.example.com\\r\\nConnection: close\\r\\n\\r\\n"
    command = (
        f"printf '{request}' | openssl s_client -tls1_2 -connect 127.0.0.1:{ats.https_port} "
        f"{session_option} {shlex.quote(str(_session_file))}")
    result = ats.run_shell(command)
    assert result.returncode == 0, result.output
    return result.output


def verify_shared_ticket(*, _ats_nodes: list[ATS], _session_file: Path) -> None:
    """Resume the first node's ticket locally and across all four peers.

    :param _ats_nodes: Test-local ats nodes configured by the test.
    :param _session_file: Test-local session file configured by the test.
    """

    outputs = [openssl_handshake(_ats_nodes[0], save=True, _session_file=_session_file)]
    outputs.append(openssl_handshake(_ats_nodes[0], save=False, _session_file=_session_file))
    outputs.extend(openssl_handshake(ats, save=False, _session_file=_session_file) for ats in _ats_nodes[1:])
    session_ids = re.findall(r"Session-ID: ([0-9A-F]+)", "\n".join(outputs))
    assert session_ids, "OpenSSL did not report a TLS session ID"
    assert len(set(session_ids)) == 1, f"session IDs were not shared: {session_ids}"


def test_stek_share(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """All stek_share peers resume the same TLS session ticket.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _cluster_ports = [services.allocate_port() for _index in range(5)]
    _ats_nodes = [configure_ats(ats_factory, index, _cluster_ports=_cluster_ports, _origin=_origin) for index in range(5)]
    _session_file = ats_factory.run_directory / "stek-session.pem"

    _origin.start()
    for ats in _ats_nodes:
        ats.start()
    wait_for_cluster(_ats_nodes=_ats_nodes)
    verify_basic_request(_ats_nodes=_ats_nodes, _curl=curl)
    verify_shared_ticket(_ats_nodes=_ats_nodes, _session_file=_session_file)
