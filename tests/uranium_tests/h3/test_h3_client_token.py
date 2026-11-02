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

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure a quiche QUIC listener that does not send Retry packets.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-h3-client-token", enable_tls=True, enable_quic=True, enable_cache=False)
    if not ats.has_feature("TS_HAS_QUICHE"):
        pytest.skip("ATS with QUICHE is required")
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
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
            "proxy.config.diags.debug.tags": "quic",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
        })
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the aioquic client that offers a token ATS never issued.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "py_h3_client" / "token_handshake.py",
            "--addr",
            f"127.0.0.1:{_ats.https_port}",
            "--server-name",
            "py.example.com",
        ),
    )


def test_h3_client_token(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Without stateless retry, a client token is not treated as proof of a Retry.

    An unvalidated token must not supply the original DCID: ATS would then
    advertise transport parameters for a Retry it never sent, which the client
    rejects, and quiche would skip its anti-amplification limit for an address
    that was never validated.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _ats = configure_ats(ats_factory)
    _client = configure_client(services, _ats=_ats)

    _ats.start()
    result = _client.run(timeout=60)
    assert result.returncode == 0, result.output
    assert "handshake completed with an unissued token" in result.stdout
