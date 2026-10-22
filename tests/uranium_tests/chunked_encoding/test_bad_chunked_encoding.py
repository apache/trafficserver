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


def unsupported_transfer_encoding_configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin that must not receive either rejected request.

    :param services: Factory owning support services and their cleanup.
    """

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


def unsupported_transfer_encoding_configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Map the rejected requests to the otherwise valid origin.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-unsupported")
    ats.records.update({"proxy.config.diags.debug.enabled": 0, "proxy.config.diags.debug.tags": "http"})
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def unsupported_transfer_encoding_request(transfer_headers: tuple[str, ...], *, _ats: ATS, _curl: Curl) -> str:
    """Send one request with the specified Transfer-Encoding field values.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param transfer_headers: Transfer headers used by this test step.
    """

    arguments = ["--header", "host: example.com"]
    for value in transfer_headers:
        arguments.extend(("--header", f"transfer-encoding: {value}"))
    arguments.extend(("--data", "stuff", f"http://127.0.0.1:{_ats.http_port}/case1", "--verbose"))
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    return result.output


def verifier_chunk_error_configure_ats(ats_factory: ATSFactory, suffix: str, *, _server: VerifierServer) -> ATS:
    """Configure a clear-text and TLS ingress for the replay.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param suffix: Suffix used by this test step.
    """

    ats = ats_factory.create(f"ts-{suffix}", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}/")
    return ats


def verifier_chunk_error_configure_client(
        services: ServiceFactory, suffix: str, *, _ats: ATS, _malformed: bool, _replay: Path) -> ProcessService:
    """Create the verifier client with the expected aggregate return code.

    :param _ats: Test-local ats configured by the test.
    :param _malformed: Test-local malformed configured by the test.
    :param _replay: Test-local replay configured by the test.
    :param services: Factory owning support services and their cleanup.
    :param suffix: Suffix used by this test step.
    """

    return services.verifier_client(
        f"client-{suffix}",
        _replay,
        http_ports=[_ats.http_port],
        https_ports=[_ats.https_port],
        return_code=1 if _malformed else 0,
        allow_errors=_malformed,
    )


def verifier_chunk_error_validate_malformed_output(client_output: str, server_output: str, *, _ats: ATS) -> None:
    """Validate every aborted malformed request and response.

    :param _ats: Test-local ats configured by the test.
    :param client_output: Client output used by this test step.
    :param server_output: Server output used by this test step.
    """

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
    assert "user agent post chunk decoding error" in _ats.traffic_out.read_text(errors="replace")


def test_unsupported_transfer_encoding(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS returns 501 for unsupported request Transfer-Encoding values.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = unsupported_transfer_encoding_configure_origin(services)
    _ats = unsupported_transfer_encoding_configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    for headers in (("gzip",), ("gzip", "chunked")):
        output = unsupported_transfer_encoding_request(headers, _ats=_ats, _curl=curl)
        assert "501 Field not implemented" in output
        assert "200 OK" not in output


def test_chunked_in_http_1_0(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Chunked encoding is rejected where HTTP/1.0 cannot carry it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    malformed = False
    replay_name = "malformed_chunked_header.replay.yaml" if malformed else "chunked_in_http_1_0.replay.yaml"
    _replay = TEST_DIRECTORY / "replays" / replay_name
    suffix = "malformed" if malformed else "http10"
    _server = services.verifier_server(f"server-{suffix}", _replay)
    _ats = verifier_chunk_error_configure_ats(ats_factory, suffix, _server=_server)
    _client = verifier_chunk_error_configure_client(services, suffix, _ats=_ats, _malformed=malformed, _replay=_replay)

    _server.start()
    _ats.start()
    result = _client.run()
    if malformed:
        verifier_chunk_error_validate_malformed_output(result.output, _server.output, _ats=_ats)


def test_malformed_chunked_header(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Malformed chunk headers abort before request or response bodies leak through.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    malformed = True
    replay_name = "malformed_chunked_header.replay.yaml" if malformed else "chunked_in_http_1_0.replay.yaml"
    _replay = TEST_DIRECTORY / "replays" / replay_name
    suffix = "malformed" if malformed else "http10"
    _server = services.verifier_server(f"server-{suffix}", _replay)
    _ats = verifier_chunk_error_configure_ats(ats_factory, suffix, _server=_server)
    _client = verifier_chunk_error_configure_client(services, suffix, _ats=_ats, _malformed=malformed, _replay=_replay)

    _server.start()
    _ats.start()
    result = _client.run()
    if malformed:
        verifier_chunk_error_validate_malformed_output(result.output, _server.output, _ats=_ats)
