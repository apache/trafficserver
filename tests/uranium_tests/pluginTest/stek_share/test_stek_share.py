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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class StekShareScenario:
    """Build a five-node STEK cluster and resume one session on every node."""

    CIPHER_SUITE = (
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

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._services = services
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._cluster_ports = [services.allocate_port() for _index in range(5)]
        self._ats_nodes = [self.configure_ats(ats_factory, index) for index in range(5)]
        self._session_file = ats_factory.run_directory / "stek-session.pem"

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the HTTP origin behind every TLS node."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "curl test"
            },
        )
        return origin

    def server_list(self) -> str:
        """Render the five dynamically allocated cluster endpoints."""

        lines = []
        for server_id, port in enumerate(self._cluster_ports, start=1):
            lines.extend((
                f"- server_id: {server_id}",
                "  address: 127.0.0.1",
                f"  port: {port}",
            ))
        return "\n".join(lines) + "\n"

    def plugin_config(self, ats: ATS, server_id: int) -> str:
        """Render one node's Raft and certificate configuration."""

        return "\n".join(
            (
                f"server_id: {server_id}",
                "address: 127.0.0.1",
                f"port: {self._cluster_ports[server_id - 1]}",
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

    def configure_ats(self, ats_factory: ATSFactory, index: int) -> ATS:
        """Configure one TLS endpoint and stek_share cluster member."""

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
        ats.write_config_file("server_list.yaml", self.server_list())
        ats.write_config_file("stek_share_conf.yaml", self.plugin_config(ats, server_id))
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "stek_share",
                "proxy.config.exec_thread.autoconfig.enabled": 0,
                "proxy.config.exec_thread.limit": 4,
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.session_ticket.enable": 1,
                "proxy.config.ssl.server.cipher_suite": self.CIPHER_SUITE,
            })
        ats.plugin_config.add_line(f"stek_share.so {ats.config_directory / 'stek_share_conf.yaml'}")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def wait_for_cluster(self) -> None:
        """Wait until every plugin reports successful initial key generation."""

        for ats in self._ats_nodes:
            wait_for_file_lines(ats.traffic_out, "Generate initial STEK succeeded", 1, timeout=20)
        time.sleep(10)

    def verify_basic_request(self) -> None:
        """Confirm the first TLS endpoint still proxies ordinary requests."""

        ats = self._ats_nodes[0]
        result = self._curl.run_for(
            ats,
            "--insecure",
            "--silent",
            "--show-error",
            "--header",
            "Host: www.example.com",
            f"https://127.0.0.1:{ats.https_port}/",
        )
        assert result.returncode == 0, result.output
        assert "curl test" in result.stdout

    def openssl_handshake(self, ats: ATS, *, save: bool) -> str:
        """Create or resume a TLS 1.2 session and return OpenSSL diagnostics."""

        session_option = "-sess_out" if save else "-sess_in"
        request = "GET / HTTP/1.1\\r\\nHost: www.example.com\\r\\nConnection: close\\r\\n\\r\\n"
        command = (
            f"printf '{request}' | openssl s_client -tls1_2 -connect 127.0.0.1:{ats.https_port} "
            f"{session_option} {shlex.quote(str(self._session_file))}")
        result = ats.run_shell(command)
        assert result.returncode == 0, result.output
        return result.output

    def verify_shared_ticket(self) -> None:
        """Resume the first node's ticket locally and across all four peers."""

        outputs = [self.openssl_handshake(self._ats_nodes[0], save=True)]
        outputs.append(self.openssl_handshake(self._ats_nodes[0], save=False))
        outputs.extend(self.openssl_handshake(ats, save=False) for ats in self._ats_nodes[1:])
        session_ids = re.findall(r"Session-ID: ([0-9A-F]+)", "\n".join(outputs))
        assert session_ids, "OpenSSL did not report a TLS session ID"
        assert len(set(session_ids)) == 1, f"session IDs were not shared: {session_ids}"

    def run(self) -> None:
        """Start the cluster, wait for STEK sync, and resume across nodes."""

        self._origin.start()
        for ats in self._ats_nodes:
            ats.start()
        self.wait_for_cluster()
        self.verify_basic_request()
        self.verify_shared_ticket()


def test_stek_share(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """All stek_share peers resume the same TLS session ticket."""

    StekShareScenario(ats_factory, services, curl).run()
