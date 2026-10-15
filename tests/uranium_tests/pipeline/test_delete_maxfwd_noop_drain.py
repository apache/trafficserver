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

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent

DELETE_NOOP_DRAIN__hostname = "www.example.com"


def configure_origin(services: ServiceFactory, *, _origin_port: int) -> ProcessService:
    """Start the purpose-built origin that detects a smuggled request.

    :param _origin_port: Test-local origin port configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "desync_server.py", "127.0.0.1", str(_origin_port)),
        ready_port=_origin_port,
    )


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Enable cache handling for the DELETE NOOP path.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=True)
    ats.remap_config.add_line(f"map http://{DELETE_NOOP_DRAIN__hostname}/ http://127.0.0.1:{_origin_port}/")
    ats.records.update({"proxy.config.http.cache.http": 1})
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Drive the body-desynchronization probe over one connection.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "desync_client_miss.py",
            "127.0.0.1",
            str(_ats.http_port),
            DELETE_NOOP_DRAIN__hostname,
        ),
    )


def verify(result: CommandResult, origin_output: str) -> None:
    """Require the NOOP response and reject all desynchronization signatures.

    :param result: Completed command result to validate.
    :param origin_output: Origin output used by this test step.
    """

    assert result.returncode == 0, result.output
    assert "DELETE_STATUS=404" in result.output
    assert "SECOND_RESPONSE_RECEIVED=True" not in result.output
    assert "misspoison" not in result.output
    assert "misspoison" not in origin_output


def test_delete_maxfwd_noop_drain(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A cache-miss DELETE self-response does not leave request bytes queued.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _origin = configure_origin(services, _origin_port=_origin_port)
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)
    _client = configure_client(services, _ats=_ats)

    _origin.start()
    _ats.start()
    result = _client.run(timeout=40)
    verify(result, _origin.output)
