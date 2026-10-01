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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

REPLAY_FILE = Path(__file__).parent / "replay" / "tls_cert_compression.replay.yaml"


def configure_server(algorithm: str, *, _services: ServiceFactory) -> VerifierServer:
    """Create the clear-text verifier origin.

    :param _services: Test-local services configured by the test.
    :param algorithm: Algorithm used by this test step.
    """

    return _services.verifier_server(f"server-{algorithm}", REPLAY_FILE)


def configure_mid(algorithm: str, server: VerifierServer, *, _ats_factory: ATSFactory) -> ATS:
    """Configure the TLS server that compresses its certificate.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param algorithm: Algorithm used by this test step.
    :param server: Server used by this test.
    """

    ats = _ats_factory.create(f"mid-{algorithm}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.server.cert_compression.algorithms": algorithm,
            "proxy.config.ssl.server.cert_compression.cache": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_cert_compress",
        })
    return ats


def configure_edge(algorithm: str, mid: ATS, *, _ats_factory: ATSFactory) -> ATS:
    """Configure the TLS client that decompresses the mid-tier certificate.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param algorithm: Algorithm used by this test step.
    :param mid: Mid used by this test step.
    """

    ats = _ats_factory.create(f"edge-{algorithm}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / https://127.0.0.1:{mid.https_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.ssl.client.cert_compression.algorithms": algorithm,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_cert_compress",
        })
    return ats


def configure_client(algorithm: str, edge: ATS, *, _services: ServiceFactory) -> ProcessService:
    """Create the verifier client that drives one exchange.

    :param _services: Test-local services configured by the test.
    :param algorithm: Algorithm used by this test step.
    :param edge: Edge used by this test step.
    """

    return _services.verifier_client(
        f"client-{algorithm}",
        REPLAY_FILE,
        http_ports=[edge.http_port],
    )


def metric(ats: ATS, name: str) -> int:
    """Read one integer ATS metric.

    :param ats: Traffic Server instance configured or queried by this step.
    :param name: Unique service or case name within this test.
    """

    result = ats.traffic_ctl("metric", "get", name)
    assert result.returncode == 0, result.output
    return int(result.stdout.split()[-1])


def run_algorithm(algorithm: str, *, _ats_factory: ATSFactory, _services: ServiceFactory) -> None:
    """Run one compression algorithm and verify success metrics.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _services: Test-local services configured by the test.
    :param algorithm: Algorithm used by this test step.
    """

    server = configure_server(algorithm, _services=_services)
    mid = configure_mid(algorithm, server, _ats_factory=_ats_factory)
    edge = configure_edge(algorithm, mid, _ats_factory=_ats_factory)
    client = configure_client(algorithm, edge, _services=_services)
    server.start()
    mid.start()
    edge.start()
    client.run()

    assert metric(mid, f"proxy.process.ssl.cert_compress.{algorithm}") == 1
    assert metric(edge, f"proxy.process.ssl.cert_decompress.{algorithm}") == 1
    assert metric(mid, f"proxy.process.ssl.cert_compress.{algorithm}_failure") == 0
    assert metric(edge, f"proxy.process.ssl.cert_decompress.{algorithm}_failure") == 0


def test_tls_cert_comp(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Edge and mid-tier ATS negotiate every supported certificate compressor.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not ats_factory.has_feature("TS_HAS_CERT_COMPRESSION_CALLBACKS"):
        pytest.skip("ATS was built without certificate compression callbacks")

    algorithms = ["zlib"]
    if ats_factory.has_feature("TS_HAS_BROTLI"):
        algorithms.append("brotli")
    if ats_factory.has_feature("TS_HAS_ZSTD"):
        algorithms.append("zstd")
    for algorithm in algorithms:
        run_algorithm(algorithm, _ats_factory=ats_factory, _services=services)
