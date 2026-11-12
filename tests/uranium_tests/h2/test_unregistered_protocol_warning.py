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
import re
import shutil
import socket
import ssl
import tempfile

from tools.uranium.services import ATS, ATSFactory, send_tcp, wait_for_file_lines

H2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
HTTP1_REQUEST = b"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"

# Every variant of the warning ends by naming what the operator should change.
WARNING = r"WARNING: Closing connection from client_ip=(?P<client>.+?) on (?P<listener>.+?): the client (?P<detected>.+)$"


def wait_for_warning(ats: ATS) -> re.Match[str]:
    """Wait for the unregistered-protocol warning and return its parsed fields.

    :param ats: Traffic Server instance whose diags.log carries the warning.
    """

    log = wait_for_file_lines(ats.diags_log, WARNING, 1)
    match = re.search(WARNING, log, re.MULTILINE)
    assert match, log
    return match


def test_h2c_prior_knowledge_on_http1_only_port(ats_factory: ATSFactory) -> None:
    """An h2c prior-knowledge client on a port without HTTP/2 is told HTTP/2 is not enabled.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.http.server_ports": f"{ats.http_port}"})
    ats.start()

    send_tcp(ats.http_port, H2_PREFACE, timeout=5)

    match = wait_for_warning(ats)
    assert match["client"] == "127.0.0.1", match[0]
    assert match["listener"] == f"local port {ats.http_port}", match[0]
    assert "sent an HTTP/2 connection preface (h2c prior knowledge), but this port does not enable HTTP/2" in match[0]
    assert "proto=" in match[0], match[0]


def test_non_h2_bytes_on_http2_only_port(ats_factory: ATSFactory) -> None:
    """Bytes that are not the h2 preface on an HTTP/2-only port are reported as handled as HTTP/1.x.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.http.server_ports": f"{ats.http_port}:proto=http2"})
    ats.start()

    send_tcp(ats.http_port, HTTP1_REQUEST, timeout=5)

    match = wait_for_warning(ats)
    assert match["client"] == "127.0.0.1", match[0]
    assert match["listener"] == f"local port {ats.http_port}", match[0]
    assert "did not send an HTTP/2 connection preface, so it was handled as HTTP/1.x" in match[0]
    assert "proto=" in match[0], match[0]


def test_h2_preface_over_tls_without_alpn(ats_factory: ATSFactory) -> None:
    """An h2 preface over TLS without ALPN is reported as a client error rather than a port setting.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update({"proxy.config.http.server_ports": f"{ats.https_port}:ssl:proto=http2"})
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.start()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    with socket.create_connection(("127.0.0.1", ats.https_port), timeout=5) as raw:
        with context.wrap_socket(raw, server_hostname="example.com") as connection:
            connection.sendall(H2_PREFACE)
            try:
                connection.recv(1024)
            except (OSError, ssl.SSLError):
                pass

    match = wait_for_warning(ats)
    assert match["listener"] == f"local port {ats.https_port}", match[0]
    assert "over TLS without negotiating h2 via ALPN" in match[0], match[0]
    assert "proto=" not in match[0], match[0]


def test_proxy_protocol_source_is_named(ats_factory: ATSFactory) -> None:
    """Behind PROXY protocol the warning names the header's source and the TCP peer.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.http.server_ports": f"{ats.http_port}:pp"})
    ats.start()

    proxy_header = f"PROXY TCP4 192.0.2.10 127.0.0.1 40000 {ats.http_port}\r\n".encode()
    send_tcp(ats.http_port, proxy_header + H2_PREFACE, timeout=5)

    match = wait_for_warning(ats)
    assert match["client"] == "192.0.2.10 (peer 127.0.0.1)", match[0]


def test_long_unix_socket_client_path(ats_factory: ATSFactory) -> None:
    """A Unix socket client bound to a long path is named in full and the listener by its path.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.http.server_ports": f"{ats.http_port} {ats.uds_path}"})
    ats.start()

    # The path is longer than an IPv6 text buffer, which once truncated it without a terminator.
    client_directory = Path(tempfile.mkdtemp(prefix="uds-client-", dir="/tmp"))
    try:
        client_path = str(client_directory / ("c" * 60))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(5)
            connection.bind(client_path)
            connection.connect(ats.uds_path)
            connection.sendall(H2_PREFACE)
            connection.shutdown(socket.SHUT_WR)
            while connection.recv(1024):
                pass
    finally:
        shutil.rmtree(client_directory, ignore_errors=True)

    match = wait_for_warning(ats)
    assert match["client"] == f"unix:{client_path}", match[0]
    assert match["listener"] == f"listener unix:{ats.uds_path}", match[0]
