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
import re

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent


class UnsupportedTransferEncodingScenario:
    """Reject non-chunked HTTP/1.1 Transfer-Encoding request fields."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the origin that must not receive either rejected request."""

        origin = services.origin("server")
        origin.add_response(
            {
                "headers": "POST /case1 HTTP/1.1\r\nHost: www.example.com\r\nuuid:1\r\n\r\n",
                "body": "stuff"
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
                "body": "more stuff",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Map the rejected requests to the otherwise valid origin."""

        ats = ats_factory.create("ts-unsupported")
        ats.records.update({"proxy.config.diags.debug.enabled": 0, "proxy.config.diags.debug.tags": "http"})
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def request(self, transfer_headers: tuple[str, ...]) -> str:
        """Send one request with the specified Transfer-Encoding field values."""

        arguments = ["--header", "host: example.com"]
        for value in transfer_headers:
            arguments.extend(("--header", f"transfer-encoding: {value}"))
        arguments.extend(("--data", "stuff", f"http://127.0.0.1:{self._ats.http_port}/case1", "--verbose"))
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
            timeout=10,
        )
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Reject gzip alone and gzip preceding chunked."""

        self._origin.start()
        self._ats.start()
        for headers in (("gzip",), ("gzip", "chunked")):
            output = self.request(headers)
            assert "501 Field not implemented" in output
            assert "200 OK" not in output


class VerifierChunkErrorScenario:
    """Run one replay covering invalid chunking on HTTP/1.0 or malformed chunk headers."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, *, malformed: bool) -> None:
        self._malformed = malformed
        replay_name = "malformed_chunked_header.replay.yaml" if malformed else "chunked_in_http_1_0.replay.yaml"
        self._replay = TEST_DIRECTORY / "replays" / replay_name
        suffix = "malformed" if malformed else "http10"
        self._server = services.verifier_server(f"server-{suffix}", self._replay)
        self._ats = self.configure_ats(ats_factory, suffix)
        self._client = self.configure_client(services, suffix)

    def configure_ats(self, ats_factory: ATSFactory, suffix: str) -> ATS:
        """Configure a clear-text and TLS ingress for the replay."""

        ats = ats_factory.create(f"ts-{suffix}", enable_tls=True, enable_cache=False)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}/")
        return ats

    def configure_client(self, services: ServiceFactory, suffix: str) -> ProcessService:
        """Create the verifier client with the expected aggregate return code."""

        return services.verifier_client(
            f"client-{suffix}",
            self._replay,
            http_ports=[self._ats.http_port],
            https_ports=[self._ats.https_port],
            return_code=1 if self._malformed else 0,
            allow_errors=self._malformed,
        )

    def validate_malformed_output(self, client_output: str, server_output: str) -> None:
        """Validate every aborted malformed request and response."""

        for key in (1, 3, 8):
            assert f"Unexpected chunked content for key {key}: too small" in server_output
        assert "chunked body of 3 bytes for key 2 with chunk stream" not in server_output
        assert "abcwxyz" not in server_output
        for key in (101, 102, 103):
            assert re.search(
                rf"(Unexpected chunked content for key {key}: too small|Failed HTTP/1 transaction with key: {key})",
                client_output,
            )
        for key in range(1, 8):
            assert f"Received an HTTP/1 400 response for key {key} with headers" in client_output
        assert "def" not in client_output
        assert "user agent post chunk decoding error" in self._ats.traffic_out.read_text(errors="replace")

    def run(self) -> None:
        """Run the replay and validate malformed connection handling when applicable."""

        self._server.start()
        self._ats.start()
        result = self._client.run()
        if self._malformed:
            self.validate_malformed_output(result.output, self._server.output)


def test_unsupported_transfer_encoding(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns 501 for unsupported request Transfer-Encoding values."""

    UnsupportedTransferEncodingScenario(ats_factory, services, curl).run()


def test_chunked_in_http_1_0(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Chunked encoding is rejected where HTTP/1.0 cannot carry it."""

    VerifierChunkErrorScenario(ats_factory, services, malformed=False).run()


def test_malformed_chunked_header(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Malformed chunk headers abort before request or response bodies leak through."""

    VerifierChunkErrorScenario(ats_factory, services, malformed=True).run()
