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
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent
HOSTS = ("www.no-oride.com", "www.ae-0.com", "www.ae-1.com", "www.ae-2.com", "www.ae-3.com", "www.ae-4.com", "www.ae-5.com")
ACCEPT_ENCODINGS = (
    "gzip",
    "x-gzip",
    "br",
    "gzip, br",
    "gzip;q=0.3, whatever;q=0.666, br;q=0.7",
    "zstd",
    "zstd, gzip",
    "zstd, br",
    "zstd, br, gzip",
    "gzip, zstd, br",
    "br, zstd",
    "zstd;q=0.8, br;q=0.7, gzip;q=0.6",
    "deflate, zstd",
    "identity, zstd, compress",
    "br, compress",
)


class NormalizeAcceptEncodingScenario:
    """Observe Accept-Encoding normalization for global and remap settings."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if not ats_factory.has_feature("TS_HAS_BROTLI"):
            pytest.skip("ATS was built without Brotli support")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._instances = (
            self.configure_ats(ats_factory, "ts"),
            self.configure_ats(ats_factory, "ts-global-zero", normalize_ae=0),
        )

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create a microserver observer that records received encodings."""

        origin = services.origin(
            "origin",
            options={"--load": TEST_DIRECTORY / "normalize_ae_observer.py"},
        )
        response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"}
        for host in HOSTS:
            origin.add_response({"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"}, response)
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str, *, normalize_ae: int | None = None) -> ATS:
        """Configure per-remap normalization modes on one ATS instance."""

        ats = ats_factory.create(name, enable_cache=False)
        if normalize_ae is not None:
            ats.records.update({"proxy.config.http.normalize_ae": normalize_ae})
        ats.remap_config.add_line(f"map http://www.no-oride.com http://127.0.0.1:{self._origin.http_port}")
        for mode in range(6):
            ats.remap_config.add_line(
                f"map http://www.ae-{mode}.com http://127.0.0.1:{self._origin.http_port} "
                f"@plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae={mode}")
        return ats

    def send_requests(self, ats: ATS, host: str) -> None:
        """Send the complete Accept-Encoding input matrix for one host."""

        base = ("--verbose", "--http1.1", "--proxy", f"localhost:{ats.http_port}")
        first = self._curl.run_for(
            ats,
            f"{shlex.join(base)} --header 'X-Au-Test: {host}' 'http://{host}'",
        )
        assert first.returncode == 0, first.output
        for value in ACCEPT_ENCODINGS:
            result = self._curl.run_for(
                ats,
                f"{shlex.join(base)} --header 'Accept-Encoding: {value}' 'http://{host}'",
            )
            assert result.returncode == 0, result.output

    def run(self) -> None:
        """Run both global configurations and compare the observer transcript."""

        self._origin.start()
        for ats in self._instances:
            ats.start()
            for host in HOSTS:
                self.send_requests(ats, host)
        assert_matches_gold(
            (self._origin.run_directory / "normalize_ae.log").read_text(errors="replace"),
            TEST_DIRECTORY / "normalize_ae.gold",
        )


def test_normalize_ae(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS normalizes Accept-Encoding according to global and remap modes."""

    NormalizeAcceptEncodingScenario(ats_factory, services, curl).run()
