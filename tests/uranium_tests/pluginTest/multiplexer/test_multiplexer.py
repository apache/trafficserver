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

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent


def select_replays(variant: str) -> tuple[Path, Path]:
    """Return the original and copy-side Proxy Verifier traffic files.

    :param variant: Multiplexer behavior variant to exercise.
    """

    names = {
        "copy-post-put": ("multiplexer_original.replay.yaml", "multiplexer_copy.replay.yaml"),
        "skip-post-put": ("multiplexer_original_skip_post.replay.yaml", "multiplexer_copy_skip_post.replay.yaml"),
        "invalid-chunk": ("multiplexer_invalid_chunk_original.replay.yaml", "multiplexer_invalid_chunk_copy.replay.yaml"),
    }
    origin, copy = names[variant]
    return TEST_DIRECTORY / "replays" / origin, TEST_DIRECTORY / "replays" / copy


def configure_servers(services: ServiceFactory, *, _copy_replay: Path,
                      _origin_replay: Path) -> tuple[VerifierServer, VerifierServer, VerifierServer]:
    """Create distinct verifier listeners for the original, HTTP copy, and HTTPS copy.

    :param services: Factory used to create the verifier servers.

    :param _copy_replay: Test-local copy replay configured by the test.
    :param _origin_replay: Test-local origin replay configured by the test.
    """

    return (
        services.verifier_server("origin", _origin_replay),
        services.verifier_server("http-copy", _copy_replay),
        services.verifier_server("https-copy", _copy_replay),
    )


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the three logical backend names to the local listeners.

    :param services: Factory used to create the DNS server.
    """

    return services.dns("dns", default="127.0.0.1")


def configure_ats(
        ats_factory: ATSFactory, *, _dns: DNSServer, _http_copy: VerifierServer, _https_copy: VerifierServer,
        _origin: VerifierServer, _variant: str) -> ATS:
    """Configure the remap plugin and its two multiplexed destinations.

    :param ats_factory: Factory used to create the Traffic Server process.

    :param _dns: Test-local dns configured by the test.
    :param _http_copy: Test-local http copy configured by the test.
    :param _https_copy: Test-local https copy configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _variant: Test-local variant configured by the test.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    if not ats.plugin_exists("multiplexer.so"):
        pytest.skip("multiplexer.so is not installed")
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|multiplexer",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
        })
    skip = " @pparam=proxy.config.multiplexer.skip_post_put=1" if _variant == "skip-post-put" else ""
    ats.remap_config.add_line(
        f"map https://origin.server.com https://backend.origin.server.com:{_origin.https_port} "
        f"@plugin=multiplexer.so @pparam=nontls.server.com @pparam=tls.server.com{skip}")
    ats.remap_config.add_line(f"map http://nontls.server.com http://backend.nontls.server.com:{_http_copy.http_port}")
    ats.remap_config.add_line(f"map http://tls.server.com https://backend.tls.server.com:{_https_copy.https_port}")
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _origin_replay: Path) -> ProcessService:
    """Drive the original HTTPS request stream through ATS.

    :param services: Factory used to create the verifier client.

    :param _ats: Test-local ats configured by the test.
    :param _origin_replay: Test-local origin replay configured by the test.
    """

    return services.verifier_client(
        "client",
        _origin_replay,
        https_ports=[_ats.https_port],
    )


def verify_server_traffic(
        *, _http_copy: VerifierServer, _https_copy: VerifierServer, _origin: VerifierServer, _variant: str) -> None:
    """Check routing coverage beyond Proxy Verifier's field assertions.

    :param _http_copy: Test-local http copy configured by the test.
    :param _https_copy: Test-local https copy configured by the test.
    :param _origin: Test-local origin configured by the test.
    :param _variant: Test-local variant configured by the test.
    """

    final_identifier = "INVALID_CHUNK" if _variant == "invalid-chunk" else "MYCUSTOMMETHOD"
    _origin.wait_for_output(rf"^uuid: {final_identifier}$")
    _http_copy.wait_for_output(rf"^uuid: {final_identifier}$")
    _https_copy.wait_for_output(rf"^uuid: {final_identifier}$")
    origin = _origin.output
    http_copy = _http_copy.output
    https_copy = _https_copy.output
    assert "X-Multiplexer: copy" not in origin
    assert "X-Multiplexer: original" in origin
    assert "X-Multiplexer: original" not in http_copy + https_copy
    assert "X-Multiplexer: copy" in http_copy
    assert "X-Multiplexer: copy" in https_copy
    assert "TLSSession" in https_copy

    if _variant == "invalid-chunk":
        assert "uuid: INVALID_CHUNK" in origin
        assert "uuid: INVALID_CHUNK" in http_copy
        assert "uuid: INVALID_CHUNK" in https_copy
        return

    for identifier in ("GET", "POST", "PUT", "CHUNKED_POST", "MYCUSTOMMETHOD"):
        assert f"uuid: {identifier}" in origin
    assert "uuid: CHUNKED_POST" not in http_copy + https_copy
    for output in (http_copy, https_copy):
        assert "uuid: GET" in output
        assert "uuid: MYCUSTOMMETHOD" in output
    if _variant == "copy-post-put":
        for output in (http_copy, https_copy):
            assert "uuid: POST" in output
            assert "uuid: PUT" in output
    else:
        assert "uuid: POST" not in http_copy + https_copy
        assert "uuid: PUT" not in http_copy + https_copy


@pytest.mark.parametrize("variant", ("copy-post-put", "skip-post-put", "invalid-chunk"))
def test_multiplexer(ats_factory: ATSFactory, services: ServiceFactory, variant: str) -> None:
    """The multiplexer plugin copies eligible requests without disrupting the original transaction.

    :param ats_factory: Factory used to create the Traffic Server process.
    :param services: Factory used to create supporting processes.
    :param variant: Multiplexer behavior variant to exercise.
    """
    _origin_replay, _copy_replay = select_replays(variant)
    _origin, _http_copy, _https_copy = configure_servers(services, _copy_replay=_copy_replay, _origin_replay=_origin_replay)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns, _http_copy=_http_copy, _https_copy=_https_copy, _origin=_origin, _variant=variant)
    _client = configure_client(services, _ats=_ats, _origin_replay=_origin_replay)

    _dns.start()
    _origin.start()
    _http_copy.start()
    _https_copy.start()
    _ats.start()
    _client.run()
    verify_server_traffic(_http_copy=_http_copy, _https_copy=_https_copy, _origin=_origin, _variant=variant)
