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

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer


def configure_server(name: str, *, _replay: Path, _services: ServiceFactory) -> VerifierServer:
    """Create an HTTP/2 TLS origin for one client-protocol case.

    :param _replay: Test-local replay configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    """

    return _services.verifier_server(name, _replay)


def configure_ats(name: str, server: VerifierServer, *, _ats_factory: ATSFactory) -> ATS:
    """Configure ATS to negotiate HTTP/2 with the origin.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param name: Unique service or case name within this test.
    :param server: Server used by this test.
    """

    ats = _ats_factory.create(name, enable_tls=True, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|http2",
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.http.server_session_sharing.pool": "thread",
            "proxy.config.http.server_session_sharing.match": "ip,sni,cert",
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    ats.remap_config.add_line(f"map / https://127.0.0.1:{server.https_port}")
    return ats


def configure_h1_client(ats: ATS, *, _directory: Path, _services: ServiceFactory) -> ProcessService:
    """Create the raw client that detects trailers after the terminal chunk.

    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    return _services.process(
        "h1-client",
        [sys.executable, _directory / "h1_trailer_client.py", "127.0.0.1",
         str(ats.http_port)],
    )


def configure_h2_client(ats: ATS, *, _replay: Path, _services: ServiceFactory) -> ProcessService:
    """Create a verifier client that expects the HTTP/2 trailer.

    :param _replay: Test-local replay configured by the test.
    :param _services: Test-local services configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    return _services.verifier_client("h2-client", _replay, https_ports=[ats.https_port])


def test_h2_origin_trailers_h1(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """HTTP/2 origin trailers are protocol-correct for HTTP/1 and HTTP/2 clients.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _directory = Path(__file__).parent
    _replay = _directory / "h2_origin_trailers_h1.replay.yaml"

    if not services.proxy_verifier_at_least("2.8.0"):
        pytest.skip("Proxy Verifier 2.8.0 or newer is required")

    h1_server = configure_server("h2-origin-h1", _replay=_replay, _services=services)
    h1_ats = configure_ats("ts-h1", h1_server, _ats_factory=ats_factory)
    h1_server.start()
    h1_ats.start()
    h1_result = configure_h1_client(h1_ats, _directory=_directory, _services=services).run()
    assert "No H2 origin trailers were forwarded to the HTTP/1 client." in h1_result.stdout

    h2_server = configure_server("h2-origin-h2", _replay=_replay, _services=services)
    h2_ats = configure_ats("ts-h2", h2_server, _ats_factory=ats_factory)
    h2_server.start()
    h2_ats.start()
    h2_result = configure_h2_client(h2_ats, _replay=_replay, _services=services).run()
    assert "x-ats-h2-trailer: smuggled" in h2_result.output
