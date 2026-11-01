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
import sys

from tools.uranium.services import ATS, ATSFactory, CommandResult, ServiceFactory, VerifierServer


def configure_server(name: str, replay: Path, *, _services: ServiceFactory) -> VerifierServer:
    """Create the verifier origin for one policy case.

    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param replay: Replay used by this test step.
    """

    return _services.verifier_server(f"server-{name}", replay)


def configure_ats(name: str, policy: int, server: VerifierServer, *, _ats_factory: ATSFactory) -> ATS:
    """Configure the stream cap and enforcement policy.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param name: Unique service or case name within this test.
    :param policy: Policy used by this test step.
    :param server: Server used by this test.
    """

    ats = _ats_factory.create(f"ts-{name}", enable_tls=True, enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2",
            "proxy.config.http2.max_active_streams_in": 2,
            "proxy.config.http2.max_active_streams_policy_in": policy,
            "proxy.config.http2.max_concurrent_streams_in": 100,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{server.http_port}")
    return ats


def run_client(name: str, ats: ATS, *, _directory: Path, _services: ServiceFactory) -> CommandResult:
    """Run the bespoke HTTP/2 client with four simultaneous streams.

    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    return _services.process(
        f"client-{name}",
        [
            sys.executable,
            _directory / "clients/h2_max_active_streams.py",
            str(ats.https_port),
            "--streams",
            "4",
            "--probe-from",
            "5",
        ],
    ).run()


def run_case(
        name: str, replay_name: str, policy: int, *, _ats_factory: ATSFactory, _directory: Path, _services: ServiceFactory) -> None:
    """Execute and validate one active-stream policy.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param replay_name: Replay name used by this test step.
    :param policy: Policy used by this test step.
    """

    server = configure_server(name, _directory / "replay" / replay_name, _services=_services)
    ats = configure_ats(name, policy, server, _ats_factory=_ats_factory)
    server.start()
    ats.start()
    result = run_client(name, ats, _directory=_directory, _services=_services)
    assert "GOAWAY" not in result.stdout
    if policy == 1:
        assert "stream 5: RST_STREAM error_code=7" in result.stdout
        assert "stream 7: RST_STREAM error_code=7" in result.stdout
        assert "active streams cap reached" in ats.traffic_out.read_text(errors="replace")
    else:
        assert "RST_STREAM error_code=7" not in result.stdout


def test_http2_max_active_streams(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The active-stream cap refuses streams without desynchronizing HPACK.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _directory = Path(__file__).parent

    run_case(
        "enforce",
        "http2_max_active_streams_enforce.replay.yaml",
        1,
        _ats_factory=ats_factory,
        _directory=_directory,
        _services=services)
    run_case(
        "advisory",
        "http2_max_active_streams_advisory.replay.yaml",
        0,
        _ats_factory=ats_factory,
        _directory=_directory,
        _services=services)
