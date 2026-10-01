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


def configure_origin(*, _directory: Path, _services: ServiceFactory) -> VerifierServer:
    """Serve the legitimate POST and a sentinel smuggled request.

    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    """

    return _services.verifier_server(
        "verifier-server",
        _directory / "replays/chunk_extension_quoted_string.replay.yaml",
    )


def configure_ats(*, _ats_factory: ATSFactory, _origin: VerifierServer) -> ATS:
    """Enable strict chunk parsing in Traffic Server.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    ats = _ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.strict_chunk_parsing": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    return ats


def run_client(name: str, *, split: bool, _ats: ATS, _directory: Path, _services: ServiceFactory) -> CommandResult:
    """Run the bespoke client once and validate the anti-smuggling result.

    :param _ats: Test-local ats configured by the test.
    :param _directory: Test-local directory configured by the test.
    :param _services: Test-local services configured by the test.
    :param name: Unique service or case name within this test.
    :param split: Split used by this test step.
    """

    command = [
        sys.executable,
        _directory / "chunk_extension_client.py",
        "127.0.0.1",
        str(_ats.http_port),
    ]
    if split:
        command.append("--split")
    result = _services.process(name, command).run()
    assert "responses=1" in result.stdout
    assert "SECOND-ENDPOINT" not in result.stdout
    return result


def test_chunk_extension_quoted_string(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A malformed chunk extension cannot smuggle a second request.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _directory = Path(__file__).parent
    _origin = configure_origin(_directory=_directory, _services=services)
    _ats = configure_ats(_ats_factory=ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    run_client("quoted-extension", split=False, _ats=_ats, _directory=_directory, _services=services)
    run_client("split-quoted-extension", split=True, _ats=_ats, _directory=_directory, _services=services)
