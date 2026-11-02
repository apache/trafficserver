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

import re
import sys
from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines


def configure_partition_server(ats_factory: ATSFactory, name: str) -> ATS:
    """Select certificates by destination while sharing fleet ticket keys.

    :param ats_factory: Factory owning the server.
    :param name: Unique process name in this test.
    """
    ats = ats_factory.create(name, enable_tls=True, enable_proxy_protocol=True)
    ats.records.update(
        {
            "proxy.config.http.server_ports":
                (
                    f"{ats.http_port}:ipv4 {ats.https_port}:ssl:ipv4 {ats.ipv6_https_port}:ssl:ipv6 "
                    f"{ats.proxy_protocol_https_port}:ssl:pp:ipv4")
        })
    ats.copy_to_ssl("ssl/server.pem", "ssl/server.key", "ssl/signed-foo.pem", "ssl/signed-foo.key", "ssl/signer.pem")
    ats.copy_to_config("file.ticket")
    entries = []
    for destination, certificate in (("127.0.0.1", "server"), ("[::1]", "signed-foo"), ("192.0.2.1", "server"),
                                     ("192.0.2.2", "signed-foo"), ("[2001:db8::1]", "server"), ("[2001:db8::2]",
                                                                                                "signed-foo"), ("*", "server")):
        entries.append({"dest_ip": destination, "ssl_cert_name": f"{certificate}.pem", "ssl_key_name": f"{certificate}.key"})
    ats.set_ssl_multicert_yaml({"ssl_multicert": entries})
    ats.records.update(
        {
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.ticket_key.filename": str(ats.config_directory / "file.ticket"),
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1,::1",
            "proxy.config.log.max_secs_per_buffer": 1
        })
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "resumption",
                        "format": "%<cqup> %<cqssr> %<cqssrt>"
                    }],
                    "logs": [{
                        "mode": "ascii",
                        "format": "resumption",
                        "filename": "resumption"
                    }]
                }
        })
    return ats


@pytest.mark.parametrize(
    "tls,case,resumed", [
        ("1.3", "same", True),
        ("1.2", "same", True),
        ("1.3", "cross", False),
        ("1.2", "cross", False),
        ("1.3", "proxy-same", True),
        ("1.3", "proxy-cross", False),
        ("1.3", "proxy6-cross", False),
        ("1.3", "shared", True),
    ])
def test_tls_resume_cert_partition(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    tls: str,
    case: str,
    resumed: bool,
) -> None:
    """Resume tickets only for the issuing certificate, including PROXY VIPs.

    :param ats_factory: Factory for isolated certificate-selecting servers.
    :param services: Factory owning the no-SNI session client.
    :param tls: TLS protocol version.
    :param case: Address/certificate and fleet-sharing scenario.
    :param resumed: Expected second-handshake resumption result.
    """
    first = configure_partition_server(ats_factory, "ts")
    second = configure_partition_server(ats_factory, "ts2") if case == "shared" else first
    first.start()
    if second is not first:
        second.start()
    first_path, second_path = f"/{case}-{tls}-first", f"/{case}-{tls}-second"
    proxy = case.startswith("proxy")
    second_ip = "::1" if case == "cross" else "127.0.0.1"
    first_port = first.proxy_protocol_https_port if proxy else first.https_port
    second_port = second.proxy_protocol_https_port if proxy else (
        second.ipv6_https_port if second_ip == "::1" else second.https_port)
    arguments = [
        sys.executable,
        Path(__file__).with_name("tls_resume_cert_partition_client.py"), "--ca", first.ssl_directory / "server.pem", "--ca",
        first.ssl_directory / "signer.pem", "--tls", tls, "--first", "127.0.0.1",
        str(first_port), first_path, "--second", second_ip,
        str(second_port), second_path
    ]
    if proxy:
        first_vip = "2001:db8::1" if case == "proxy6-cross" else "192.0.2.1"
        second_vip = ("2001:db8::2" if case == "proxy6-cross" else "192.0.2.2") if not resumed else first_vip
        arguments += ["--first-proxy-dst", first_vip, "--second-proxy-dst", second_vip]
    result = services.process("client", arguments).run(timeout=30)
    assert result.returncode == 0, result.output
    assert f"{first_path}: CN=random.server.com reused=False " in result.output
    second_cn = "random.server.com" if resumed else "foo.com"
    assert f"{second_path}: CN={second_cn} reused={resumed} " in result.output
    expected = second_path.lstrip("/") + (" 1 2" if resumed else " 0 0")
    wait_for_file_lines(second.log_directory / "resumption.log", re.escape(expected), 1)
