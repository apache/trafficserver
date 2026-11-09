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
"""A large HTTP/3 response is not limited to OpenSSL's initial stream send buffer."""

from pathlib import Path
import re

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent

H3_LARGE_RESPONSE__replay = "replays/h3_large_response.replay.yaml"

# OpenSSL creates each stream's send buffer at this size and grows it only when
# one write offers more data than the buffer has room for. The buffer holds data
# until the peer acknowledges it, so a stream that never grows it can have no
# more than this many bytes in flight.
OPENSSL_INITIAL_STREAM_SEND_BUFFER = 8192

STREAM_BUFFERED_LINE = r"stream [0-9]+: OpenSSL holds ([0-9]+) bytes to send or to be acknowledged"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin that answers with a 1 MB body.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server(
        "server-h3-stream-send-buffer", H3_LARGE_RESPONSE__replay, verbose=False, other_args="--poll-timeout 30000"
    )


def configure_ats(ats_factory: ATSFactory, *, _server: VerifierServer) -> ATS:
    """Configure HTTP/3 with v_quic_net debugging on, so each stream write logs how much OpenSSL holds for the stream.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-h3-stream-send-buffer", enable_tls=True, enable_quic=True, enable_cache=False)
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
            "proxy.config.diags.debug.tags": "v_quic_net",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
        }
    )
    ats.remap_config.add_line(f"map https://example.com/ http://127.0.0.1:{_server.http_port}/")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the verifier client that fetches the large response over HTTP/3.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_client(
        "client-h3-stream-send-buffer",
        H3_LARGE_RESPONSE__replay,
        http3_ports=[_ats.https_port],
        verbose=False,
        poll_timeout=30000,
    )


def test_h3_stream_send_buffer_grows(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS offers a stream more data than OpenSSL's initial send buffer holds, so OpenSSL grows the buffer.

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

    content = wait_for_file_lines(_ats.traffic_out, STREAM_BUFFERED_LINE, 1)
    most_buffered = max(int(held) for held in re.findall(STREAM_BUFFERED_LINE, content))
    assert most_buffered > OPENSSL_INITIAL_STREAM_SEND_BUFFER, (
        f"OpenSSL never held more than {most_buffered} bytes for a stream sending a 1 MB response; "
        f"its send buffer stayed at the initial {OPENSSL_INITIAL_STREAM_SEND_BUFFER} bytes"
    )
