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
import time

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    DNSServer,
    OriginServer,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    assert_matches_gold,
)

TEST_DIRECTORY = Path(__file__).parent


class ChunkedEncodingScenario:
    """Exercise chunked response conversion and a request-smuggling regression."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._services = services
        self._curl = curl
        if not curl.supports("http2"):
            pytest.skip("curl HTTP/2 support is required")
        self._smuggle_port = services.allocate_port()
        self._server = self.configure_origin(services, "server", ssl=False, body="", host="www.example.com")
        self._tls_server = self.configure_origin(
            services,
            "server-tls",
            ssl=True,
            body="12345678901234567890",
            host="www.anotherexample.com",
        )
        self._post_server = self.configure_origin(
            services,
            "server-post",
            ssl=False,
            body="",
            host="www.yetanotherexample.com",
        )
        self._smuggle_server = self.configure_smuggle_server(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(
        services: ServiceFactory,
        name: str,
        *,
        ssl: bool,
        body: str,
        host: str,
    ) -> OriginServer:
        """Create one origin that emits a chunked response."""

        origin = services.origin(name, ssl=ssl)
        method = "GET" if host == "www.example.com" else "POST"
        request_body = "" if method == "GET" else "knock knock"
        request = {"headers": f"{method} / HTTP/1.1\r\nHost: {host}\r\n\r\n", "body": request_body}
        origin.add_response(
            request,
            {
                "headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
                "body": body,
            },
        )
        return origin

    def configure_smuggle_server(self, services: ServiceFactory) -> ProcessService:
        """Create the one-shot origin that captures any smuggled bytes."""

        return services.process(
            "smuggle-server",
            ["bash", TEST_DIRECTORY / "server4.sh", str(self._smuggle_port), "outserver4"],
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure clear-text, TLS-origin, and smuggling remaps."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_lines(
            (
                f"map http://www.example.com http://127.0.0.1:{self._server.port}",
                f"map http://www.yetanotherexample.com http://127.0.0.1:{self._post_server.port}",
                f"map https://www.anotherexample.com https://127.0.0.1:{self._tls_server.https_port}",
                f"map / http://127.0.0.1:{self._smuggle_port}",
            ))
        return ats

    def curl_request(self, *arguments: str, gold: str) -> None:
        """Run curl and compare its protocol diagnostics with a gold file."""

        result = self._curl.run_for(self._ats, *arguments, timeout=10)
        assert result.returncode == 0, result.output
        assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / gold)

    def run(self) -> None:
        """Run chunked GET/POST conversions followed by the smuggling probe."""

        self._server.start()
        self._tls_server.start()
        self._post_server.start()
        self._ats.start()
        if self._curl.uses_uds:
            first_args = ("--http1.1", "--header", "Host: www.example.com", f"http://127.0.0.1:{self._ats.http_port}", "--verbose")
            first_gold = "chunked_GET_200_uds.gold"
        else:
            first_args = (
                "--http1.1",
                "--proxy",
                f"127.0.0.1:{self._ats.http_port}",
                "http://www.example.com",
                "--verbose",
            )
            first_gold = "chunked_GET_200.gold"
        self.curl_request(*first_args, gold=first_gold)

        if not self._curl.uses_uds:
            self.curl_request(
                "--http2",
                "--insecure",
                f"https://127.0.0.1:{self._ats.https_port}",
                "--verbose",
                "--header",
                "Host: www.anotherexample.com",
                "--data",
                "Knock knock",
                gold="h2_chunked_POST_200.gold",
            )
        for extra in ((), ("--header", "Transfer-Encoding: chunked")):
            self.curl_request(
                f"http://127.0.0.1:{self._ats.http_port}",
                "--header",
                "Host: www.yetanotherexample.com",
                "--verbose",
                *extra,
                "--data",
                "Knock knock",
                gold="chunked_POST_200.gold",
            )

        self._smuggle_server.start()
        time.sleep(0.1)
        smuggle_client = self._services.resolve_path("smuggle-client")
        result = self._services.process(
            "smuggle-client",
            [smuggle_client, "127.0.0.1", str(self._ats.https_port)],
        ).run(timeout=10)
        assert "content-length:" not in result.output.lower()
        self._smuggle_server.wait(timeout=10)
        captured = (self._smuggle_server.run_directory / "outserver4").read_text(errors="replace")
        assert "sneaky" not in captured


class ChunkedTrailersScenario:
    """Verify the default dropped and explicitly proxied trailer policies."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services

    def run_case(self, *, drop_trailers: bool) -> None:
        """Run one trailer policy through Proxy Verifier."""

        suffix = "drop" if drop_trailers else "proxy"
        replay_name = "chunked_trailer_dropped.replay.yaml" if drop_trailers else "chunked_trailer_proxied.replay.yaml"
        replay = TEST_DIRECTORY / "replays" / replay_name
        dns: DNSServer = self._services.dns(f"dns-{suffix}", default="127.0.0.1")
        server: VerifierServer = self._services.verifier_server(f"server-{suffix}", replay)
        ats = self._ats_factory.create(f"ts-{suffix}", enable_cache=False)
        ats.remap_config.add_line(f"map / http://backend.example.com:{server.http_port}/")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        if not drop_trailers:
            ats.records.update({"proxy.config.http.drop_chunked_trailers": 0})
        client = self._services.verifier_client(f"client-{suffix}", replay, http_ports=[ats.http_port])
        dns.start()
        server.start()
        ats.start()
        result = client.run()
        if drop_trailers:
            assert "Client: ATS" not in server.output
            assert 'ETag: "abc"' not in server.output
            assert "Sever: ATS" not in result.output
            assert 'ETag: "def"' not in result.output
        else:
            assert "Client: ATS" in server.output
            assert 'ETag: "abc"' in server.output
            assert "Sever: ATS" in result.output
            assert 'ETag: "def"' in result.output

    def run(self) -> None:
        """Run both supported trailer policies."""

        self.run_case(drop_trailers=True)
        self.run_case(drop_trailers=False)


def test_chunked_encoding(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS processes chunked bodies without permitting request smuggling."""

    ChunkedEncodingScenario(ats_factory, services, curl).run()


def test_chunked_trailers(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Chunked trailers are dropped by default and proxied when enabled."""

    ChunkedTrailersScenario(ats_factory, services).run()
