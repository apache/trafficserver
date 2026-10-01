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

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_metric

REPLAY_FILE = Path(__file__).parent / "replay" / "tls_cert_compression_cache.replay.yaml"


def configure_server(suffix: str, *, _services: ServiceFactory) -> VerifierServer:
    """Create the clear-text origin for one cache mode.

    :param _services: Test-local services configured by the test.
    :param suffix: Suffix used by this test step.
    """

    return _services.verifier_server(f"server-{suffix}", REPLAY_FILE)


def configure_mid(suffix: str, server: VerifierServer, cache_enabled: bool, *, _ats_factory: ATSFactory) -> ATS:
    """Configure the TLS server that compresses its certificate.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param suffix: Suffix used by this test step.
    :param server: Server used by this test.
    :param cache_enabled: Cache enabled used by this test step.
    """

    ats = _ats_factory.create(f"mid-{suffix}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.server.cert_compression.algorithms": "zlib",
            "proxy.config.ssl.server.cert_compression.cache": int(cache_enabled),
            "proxy.config.ssl.server.session_ticket.enable": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_cert_compress",
        })
    return ats


def configure_edge(suffix: str, mid: ATS, *, _ats_factory: ATSFactory) -> ATS:
    """Configure the TLS client that decompresses the mid-tier certificate.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param suffix: Suffix used by this test step.
    :param mid: Mid used by this test step.
    """

    ats = _ats_factory.create(f"edge-{suffix}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / https://127.0.0.1:{mid.https_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.ssl.client.cert_compression.algorithms": "zlib",
            "proxy.config.http.keep_alive_enabled_out": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_cert_compress",
        })
    return ats


def configure_client(suffix: str, edge: ATS, *, _services: ServiceFactory) -> ProcessService:
    """Create a verifier client with two separate sessions.

    :param _services: Test-local services configured by the test.
    :param suffix: Suffix used by this test step.
    :param edge: Edge used by this test step.
    """

    return _services.verifier_client(f"client-{suffix}", REPLAY_FILE, http_ports=[edge.http_port])


def run_case(cache_enabled: bool, *, _ats_factory: ATSFactory, _services: ServiceFactory) -> None:
    """Run two handshakes and verify compression and cache metrics.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _services: Test-local services configured by the test.
    :param cache_enabled: Cache enabled used by this test step.
    """

    suffix = "enabled" if cache_enabled else "disabled"
    server = configure_server(suffix, _services=_services)
    mid = configure_mid(suffix, server, cache_enabled, _ats_factory=_ats_factory)
    edge = configure_edge(suffix, mid, _ats_factory=_ats_factory)
    client = configure_client(suffix, edge, _services=_services)
    server.start()
    mid.start()
    edge.start()
    client.run()
    wait_for_metric(mid, "proxy.process.ssl.cert_compress.zlib", 2)
    wait_for_metric(edge, "proxy.process.ssl.cert_decompress.zlib", 2)
    wait_for_metric(mid, "proxy.process.ssl.cert_compress.zlib_failure", 0)
    wait_for_metric(mid, "proxy.process.ssl.cert_compress.cache_hit", int(cache_enabled))


def test_tls_cert_comp_cache(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Certificate compression caches reuse exactly one of two results.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not ats_factory.has_feature("TS_HAS_CERT_COMPRESSION_CALLBACKS"):
        pytest.skip("ATS was built without certificate compression callbacks")

    run_case(True, _ats_factory=ats_factory, _services=services)
    run_case(False, _ats_factory=ats_factory, _services=services)
