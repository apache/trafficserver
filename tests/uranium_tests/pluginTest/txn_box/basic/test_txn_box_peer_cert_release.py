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
"""Verify txn_box's client certificate extractors release the peer certificate.

The ssl_peer_cert_release test plugin counts client certificates when a
transaction closes and again when OpenSSL frees them. A certificate reference
taken by txn_box and never dropped keeps the certificate alive after its
connection closes, so the released count stays behind the tagged count.
"""

from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory
from tools.uranium.services.service_utils import wait_for_metric

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY.parent / "ssl"

# Each request is its own curl process and thus its own TLS connection with a
# distinct peer certificate object in ATS.
CONNECTION_COUNT = 3

TXN_BOX_CONFIG = """\
txn_box:
- when: proxy-rsp
  do:
  - proxy-rsp-field<X-Client-Subject>: inbound-cert-remote-subject-field<CN>
  - proxy-rsp-field<X-Client-Issuer>: inbound-cert-remote-issuer-field<CN>
"""


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin that answers every proxied request.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET /cert HTTP/1.1\r\nHost: base.ex\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, origin: OriginServer) -> ATS:
    """Configure ATS to require client certificates and read their fields with txn_box.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param origin: Origin the remap rule forwards to.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    if not ats.plugin_exists("txn_box.so"):
        pytest.skip("txn_box.so is required")
    ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "ca-bundle.pem")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "txn_box|ssl_peer_cert_release",
            "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "ca-bundle.pem"),
            "proxy.config.ssl.client.certification_level": 2,
            # A server-side session would hold its own peer certificate reference beyond the connection.
            "proxy.config.ssl.server.session_ticket.enable": 0,
        })
    ats.write_config_file("peer_cert.txnbox.yaml", TXN_BOX_CONFIG)
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_peer_cert_release.so")
    ats.plugin_config.add_line("ssl_peer_cert_release.so")
    ats.plugin_config.add_line(f"txn_box.so --key txn_box {ats.config_directory / 'peer_cert.txnbox.yaml'}")
    ats.remap_config.add_line(f"map https://base.ex/ http://127.0.0.1:{origin.port}/")
    return ats


def request(ats: ATS, curl: Curl) -> None:
    """Send one mTLS request and check txn_box read both certificate fields.

    :param ats: Traffic Server receiving the request.
    :param curl: Transport-aware curl command runner.
    """

    result = curl.run_for(
        ats,
        (
            f"-v --http1.1 --insecure --cert '{SSL_DIRECTORY / 'alpha-signed.pem'}' "
            f"--key '{SSL_DIRECTORY / 'alpha-signed.key'}' "
            f"--resolve 'base.ex:{ats.https_port}:127.0.0.1' 'https://base.ex:{ats.https_port}/cert'"),
    )
    assert result.returncode == 0, result.output
    assert "X-Client-Subject: base.ex:role.alpha" in result.output, result.output
    assert "X-Client-Issuer: TxnBox CA alpha" in result.output, result.output


def test_txn_box_peer_cert_release(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Client certificates are freed after txn_box reads their subject and issuer fields.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    if curl.uses_uds:
        pytest.skip("client certificates require a TCP TLS listener")
    origin = configure_origin(services)
    ats = configure_ats(ats_factory, origin)

    origin.start()
    ats.start()
    for _ in range(CONNECTION_COUNT):
        request(ats, curl)

    wait_for_metric(ats, "plugin.ssl_peer_cert_release.tagged", CONNECTION_COUNT)
    wait_for_metric(ats, "plugin.ssl_peer_cert_release.released", CONNECTION_COUNT)
