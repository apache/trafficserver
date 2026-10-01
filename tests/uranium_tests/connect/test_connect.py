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
import re

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    HttpBinServer,
    ProcessService,
    ServiceFactory,
    VerifierServer,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent


def curl_connect_configure_origin(services: ServiceFactory) -> HttpBinServer:
    """Create the HTTP origin reached after CONNECT succeeds.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("httpbin")


def curl_connect_configure_ats(ats_factory: ATSFactory, *, _origin: HttpBinServer) -> ATS:
    """Allow CONNECT only to the allocated origin port.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.server_ports": str(ats.http_port),
            "proxy.config.http.connect_ports": str(_origin.port),
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map http://foo.com/ http://127.0.0.1:{_origin.port}/")
    ats.allow_private_connect()
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats":
                        [{
                            "name": "common",
                            "format": '%<chi> - %<caun> [%<cqtn>] "%<cqhm> %<pqu> %<cqpv>" %<pssc> %<pscl>',
                        }],
                    "logs": [{
                        "filename": "access",
                        "format": "common"
                    }],
                }
        })
    return ats


def verifier_connect_configure_server(services: ServiceFactory, suffix: str, *, _replay: Path) -> VerifierServer:
    """Create the verifier tunnel destination.

    :param _replay: Test-local replay configured by the test.
    :param services: Factory owning support services and their cleanup.
    :param suffix: Suffix used by this test step.
    """

    return services.verifier_server(f"connect-server-{suffix}", _replay)


def verifier_connect_configure_ats(ats_factory: ATSFactory, suffix: str, *, _server: VerifierServer, _use_http2: bool) -> ATS:
    """Configure a listener and CONNECT ACL for the verifier origin.

    :param _server: Test-local server configured by the test.
    :param _use_http2: Test-local use http2 configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param suffix: Suffix used by this test step.
    """

    ats = ats_factory.create(f"connect-ts-{suffix}", enable_tls=_use_http2)
    if _use_http2:
        ats.add_default_ssl_files()
        server_ports = f"{ats.https_port}:ssl"
        tags = "http|hpack"
    else:
        server_ports = str(ats.http_port)
        tags = "http|iocore_net|rec"
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": tags,
            "proxy.config.http.server_ports": server_ports,
            "proxy.config.http.connect_ports": str(_server.http_port),
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}/")
    ats.allow_private_connect()
    return ats


def verifier_connect_configure_client(
        services: ServiceFactory, suffix: str, *, _ats: ATS, _replay: Path, _use_http2: bool) -> ProcessService:
    """Create the verifier client for the selected inbound protocol.

    :param _ats: Test-local ats configured by the test.
    :param _replay: Test-local replay configured by the test.
    :param _use_http2: Test-local use http2 configured by the test.
    :param services: Factory owning support services and their cleanup.
    :param suffix: Suffix used by this test step.
    """

    options = {"https_ports": [_ats.https_port]} if _use_http2 else {"http_ports": [_ats.http_port]}
    return services.verifier_client(f"connect-client-{suffix}", _replay, **options)


def verifier_connect_verify_server_output(*, _server: VerifierServer, _use_http2: bool) -> None:
    """Require the tunneled request and exclude the CONNECT metadata at the origin.

    :param _server: Test-local server configured by the test.
    :param _use_http2: Test-local use http2 configured by the test.
    """

    if _use_http2:
        assert "test: connect-request" not in _server.output
        assert re.search(r"GET /get HTTP/1\.1\nuuid: 1\ntest: real-request", _server.output)
    else:
        assert "uuid: 1" not in _server.output
        assert re.search(r"GET /get HTTP/1\.1\nuuid: 2", _server.output)


def verifier_connect_verify_metrics(*, _ats: ATS) -> None:
    """Compare the HTTP/1.1 tunnel connection metrics with their gold file.

    :param _ats: Test-local ats configured by the test.
    """

    gold = TEST_DIRECTORY / "gold" / "metrics.gold"
    names = [line.split()[0] for line in gold.read_text().splitlines()]
    result = _ats.traffic_ctl("metric", "get", *names)
    assert result.returncode == 0, result.output
    assert_matches_gold(result.stdout, gold)


def test_connect_curl(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """curl can tunnel an HTTP request through ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = curl_connect_configure_origin(services)
    _ats = curl_connect_configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.run_for(
        _ats,
        f"--verbose --fail --silent --proxytunnel --proxy '127.0.0.1:{_ats.http_port}' http://foo.com/get",
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / "connect_0_stderr.gold")
    access_log = wait_for_file_lines(_ats.log_directory / "access.log", "CONNECT", 1)
    assert_matches_gold(access_log, TEST_DIRECTORY / "gold" / "connect_access.gold")


def test_connect_verifier_http1(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Proxy Verifier can carry HTTP/1.1 through an ATS CONNECT tunnel.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    use_http2 = False
    replay_name = "connect_h2.replay.yaml" if use_http2 else "connect.replay.yaml"
    _replay = TEST_DIRECTORY / "replays" / replay_name
    suffix = "h2" if use_http2 else "h1"
    _server = verifier_connect_configure_server(services, suffix, _replay=_replay)
    _ats = verifier_connect_configure_ats(ats_factory, suffix, _server=_server, _use_http2=use_http2)
    _client = verifier_connect_configure_client(services, suffix, _ats=_ats, _replay=_replay, _use_http2=use_http2)

    _server.start()
    _ats.start()
    _client.run()
    verifier_connect_verify_server_output(_server=_server, _use_http2=use_http2)
    traffic_output = _ats.traffic_out.read_text(errors="replace")
    assert re.search(
        rf"Proxy's Request.*\n.*\nCONNECT 127\.0\.0\.1:{_server.http_port} HTTP/1\.1",
        traffic_output,
    )
    if not use_http2:
        verifier_connect_verify_metrics(_ats=_ats)


def test_connect_verifier_http2(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Proxy Verifier can carry HTTP/1.1 inside an HTTP/2 CONNECT stream.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    use_http2 = True
    replay_name = "connect_h2.replay.yaml" if use_http2 else "connect.replay.yaml"
    _replay = TEST_DIRECTORY / "replays" / replay_name
    suffix = "h2" if use_http2 else "h1"
    _server = verifier_connect_configure_server(services, suffix, _replay=_replay)
    _ats = verifier_connect_configure_ats(ats_factory, suffix, _server=_server, _use_http2=use_http2)
    _client = verifier_connect_configure_client(services, suffix, _ats=_ats, _replay=_replay, _use_http2=use_http2)

    _server.start()
    _ats.start()
    _client.run()
    verifier_connect_verify_server_output(_server=_server, _use_http2=use_http2)
    traffic_output = _ats.traffic_out.read_text(errors="replace")
    assert re.search(
        rf"Proxy's Request.*\n.*\nCONNECT 127\.0\.0\.1:{_server.http_port} HTTP/1\.1",
        traffic_output,
    )
    if not use_http2:
        verifier_connect_verify_metrics(_ats=_ats)
