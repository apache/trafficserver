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
"""Verify the Proxy Protocol allowlist distinguishes prefaced traffic."""

from pathlib import Path
import shlex

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent

PROXY_PROTOCOL_ALLOWLIST_REPLAY = TEST_DIRECTORY / "replay" / "proxy_protocol_allowlist.replay.yaml"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin for the two ordinary requests.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("origin", PROXY_PROTOCOL_ALLOWLIST_REPLAY)


def configure_ats(ats_factory: ATSFactory, *, _server: VerifierServer) -> ATS:
    """Allow Proxy Protocol only from an address other than loopback.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ats", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}/")
    ats.records.update(
        {
            "proxy.config.http.proxy_protocol_allowlist": "192.0.2.1",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol",
        })
    return ats


def request(*, tls: bool, proxy_protocol: bool, uuid: str | None = None, _ats: ATS, _curl: Curl) -> int:
    """Issue one request and return curl's status.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param tls: Tls used by this test step.
    :param proxy_protocol: Proxy protocol used by this test step.
    :param uuid: Uuid used by this test step.
    """

    port = _ats.proxy_protocol_https_port if tls else _ats.proxy_protocol_port
    arguments = ["--silent", "--show-error", "--output", "/dev/null", "--max-time", "5"]
    if tls:
        arguments.append("--insecure")
    if proxy_protocol:
        arguments.append("--haproxy-protocol")
    if uuid is not None:
        arguments.extend(("--header", f"uuid: {uuid}"))
    arguments.append(f"{'https' if tls else 'http'}://127.0.0.1:{port}/get")
    return _curl.run_for(
        _ats,
        shlex.join(arguments),
        timeout=10,
    ).returncode


def test_proxy_protocol_allowlist(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The allowlist applies only when a peer sends a Proxy Protocol header.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _server=_server)
    _curl = Curl(ats_factory.run_directory)

    _server.start()
    _ats.start()
    assert request(tls=False, proxy_protocol=False, uuid="1", _ats=_ats, _curl=_curl) == 0
    assert request(tls=True, proxy_protocol=False, uuid="2", _ats=_ats, _curl=_curl) == 0
    assert request(tls=False, proxy_protocol=True, _ats=_ats, _curl=_curl) in (52, 56)
    assert request(tls=True, proxy_protocol=True, _ats=_ats, _curl=_curl) in (35, 52, 56)
