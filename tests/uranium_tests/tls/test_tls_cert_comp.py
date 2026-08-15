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


class CertificateCompressionScenario:
    """Negotiate RFC 8879 compression between edge and mid-tier ATS."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        if not ats_factory.has_feature("TS_HAS_CERT_COMPRESSION_CALLBACKS"):
            pytest.skip("ATS was built without certificate compression callbacks")

    def configure_server(self, algorithm: str) -> VerifierServer:
        """Create the clear-text verifier origin."""

        return self._services.verifier_server(f"server-{algorithm}", REPLAY_FILE)

    def configure_mid(self, algorithm: str, server: VerifierServer) -> ATS:
        """Configure the TLS server that compresses its certificate."""

        ats = self._ats_factory.create(f"mid-{algorithm}", enable_tls=True, enable_cache=False)
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

    def configure_edge(self, algorithm: str, mid: ATS) -> ATS:
        """Configure the TLS client that decompresses the mid-tier certificate."""

        ats = self._ats_factory.create(f"edge-{algorithm}", enable_tls=True, enable_cache=False)
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

    def configure_client(self, algorithm: str, edge: ATS) -> ProcessService:
        """Create the verifier client that drives one exchange."""

        return self._services.verifier_client(
            f"client-{algorithm}",
            REPLAY_FILE,
            http_ports=[edge.http_port],
        )

    @staticmethod
    def metric(ats: ATS, name: str) -> int:
        """Read one integer ATS metric."""

        result = ats.traffic_ctl("metric", "get", name)
        assert result.returncode == 0, result.output
        return int(result.stdout.split()[-1])

    def run_algorithm(self, algorithm: str) -> None:
        """Run one compression algorithm and verify success metrics."""

        server = self.configure_server(algorithm)
        mid = self.configure_mid(algorithm, server)
        edge = self.configure_edge(algorithm, mid)
        client = self.configure_client(algorithm, edge)
        server.start()
        mid.start()
        edge.start()
        client.run()

        assert self.metric(mid, f"proxy.process.ssl.cert_compress.{algorithm}") == 1
        assert self.metric(edge, f"proxy.process.ssl.cert_decompress.{algorithm}") == 1
        assert self.metric(mid, f"proxy.process.ssl.cert_compress.{algorithm}_failure") == 0
        assert self.metric(edge, f"proxy.process.ssl.cert_decompress.{algorithm}_failure") == 0

    def run(self) -> None:
        """Exercise every compression algorithm compiled into ATS."""

        algorithms = ["zlib"]
        if self._ats_factory.has_feature("TS_HAS_BROTLI"):
            algorithms.append("brotli")
        if self._ats_factory.has_feature("TS_HAS_ZSTD"):
            algorithms.append("zstd")
        for algorithm in algorithms:
            self.run_algorithm(algorithm)


def test_tls_cert_comp(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Edge and mid-tier ATS negotiate every supported certificate compressor."""

    CertificateCompressionScenario(ats_factory, services).run()
