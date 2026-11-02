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


def chunked_encoding_configure_origin(
    services: ServiceFactory,
    name: str,
    *,
    ssl: bool,
    body: str,
    host: str,
) -> OriginServer:
    """Create one origin that emits a chunked response.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    :param ssl: Ssl used by this test step.
    :param body: HTTP message body.
    :param host: HTTP host name used for the request.
    """

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


def chunked_encoding_configure_smuggle_server(services: ServiceFactory, *, _smuggle_port: int) -> ProcessService:
    """Create the one-shot origin that captures any smuggled bytes.

    :param _smuggle_port: Test-local smuggle port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "smuggle-server",
        ["bash", TEST_DIRECTORY / "server4.sh", str(_smuggle_port), "outserver4"],
    )


def chunked_encoding_configure_ats(
        ats_factory: ATSFactory, *, _post_server: OriginServer, _server: OriginServer, _smuggle_port: int,
        _tls_server: OriginServer) -> ATS:
    """Configure clear-text, TLS-origin, and smuggling remaps.

    :param _post_server: Test-local post server configured by the test.
    :param _server: Test-local server configured by the test.
    :param _smuggle_port: Test-local smuggle port configured by the test.
    :param _tls_server: Test-local tls server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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
            f"map http://www.example.com http://127.0.0.1:{_server.port}",
            f"map http://www.yetanotherexample.com http://127.0.0.1:{_post_server.port}",
            f"map https://www.anotherexample.com https://127.0.0.1:{_tls_server.https_port}",
            f"map / http://127.0.0.1:{_smuggle_port}",
        ))
    return ats


def chunked_encoding_curl_request(*arguments: str, gold: str, _ats: ATS, _curl: Curl) -> None:
    """Run curl and compare its protocol diagnostics with a gold file.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param gold: Gold used by this test step.
    :param arguments: Arguments used by this test step.
    """

    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / gold)


def chunked_trailers_run_case(*, drop_trailers: bool, _ats_factory: ATSFactory, _services: ServiceFactory) -> None:
    """Run one trailer policy through Proxy Verifier.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _services: Test-local services configured by the test.
    :param drop_trailers: Drop trailers used by this test step.
    """

    suffix = "drop" if drop_trailers else "proxy"
    replay_name = "chunked_trailer_dropped.replay.yaml" if drop_trailers else "chunked_trailer_proxied.replay.yaml"
    replay = TEST_DIRECTORY / "replays" / replay_name
    dns: DNSServer = _services.dns(f"dns-{suffix}", default="127.0.0.1")
    server: VerifierServer = _services.verifier_server(f"server-{suffix}", replay)
    ats = _ats_factory.create(f"ts-{suffix}", enable_cache=False)
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
    client = _services.verifier_client(f"client-{suffix}", replay, http_ports=[ats.http_port])
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


def test_chunked_encoding(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS processes chunked bodies without permitting request smuggling.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if not curl.supports("http2"):
        pytest.skip("curl HTTP/2 support is required")
    _smuggle_port = services.allocate_port()
    _server = chunked_encoding_configure_origin(services, "server", ssl=False, body="", host="www.example.com")
    _tls_server = chunked_encoding_configure_origin(
        services,
        "server-tls",
        ssl=True,
        body="12345678901234567890",
        host="www.anotherexample.com",
    )
    _post_server = chunked_encoding_configure_origin(
        services,
        "server-post",
        ssl=False,
        body="",
        host="www.yetanotherexample.com",
    )
    _smuggle_server = chunked_encoding_configure_smuggle_server(services, _smuggle_port=_smuggle_port)
    _ats = chunked_encoding_configure_ats(
        ats_factory, _post_server=_post_server, _server=_server, _smuggle_port=_smuggle_port, _tls_server=_tls_server)

    _server.start()
    _tls_server.start()
    _post_server.start()
    _ats.start()
    if curl.uses_uds:
        first_args = ("--http1.1", "--header", "Host: www.example.com", f"http://127.0.0.1:{_ats.http_port}", "--verbose")
        first_gold = "chunked_GET_200_uds.gold"
    else:
        first_args = (
            "--http1.1",
            "--proxy",
            f"127.0.0.1:{_ats.http_port}",
            "http://www.example.com",
            "--verbose",
        )
        first_gold = "chunked_GET_200.gold"
    chunked_encoding_curl_request(*first_args, gold=first_gold, _ats=_ats, _curl=curl)

    if not curl.uses_uds:
        chunked_encoding_curl_request(
            "--http2",
            "--insecure",
            f"https://127.0.0.1:{_ats.https_port}",
            "--verbose",
            "--header",
            "Host: www.anotherexample.com",
            "--data",
            "Knock knock",
            gold="h2_chunked_POST_200.gold",
            _ats=_ats,
            _curl=curl)
    for extra in ((), ("--header", "Transfer-Encoding: chunked")):
        chunked_encoding_curl_request(
            f"http://127.0.0.1:{_ats.http_port}",
            "--header",
            "Host: www.yetanotherexample.com",
            "--verbose",
            *extra,
            "--data",
            "Knock knock",
            gold="chunked_POST_200.gold",
            _ats=_ats,
            _curl=curl)

    _smuggle_server.start()
    time.sleep(0.1)
    smuggle_client = services.resolve_path("smuggle-client")
    result = services.process(
        "smuggle-client",
        [smuggle_client, "127.0.0.1", str(_ats.https_port)],
    ).run(timeout=10)
    assert "content-length:" not in result.output.lower()
    _smuggle_server.wait(timeout=10)
    captured = (_smuggle_server.run_directory / "outserver4").read_text(errors="replace")
    assert "sneaky" not in captured


def test_chunked_trailers(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Chunked trailers are dropped by default and proxied when enabled.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    chunked_trailers_run_case(drop_trailers=True, _ats_factory=ats_factory, _services=services)
    chunked_trailers_run_case(drop_trailers=False, _ats_factory=ats_factory, _services=services)
