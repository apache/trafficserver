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

from tools.uranium.services import ATS, ATSFactory, Curl, ProcessService, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory, *, _origin_port: int, _ready_file: Path) -> ProcessService:
    """Start the one-shot clear-text netcat server used as a TLS origin.

    :param _origin_port: Test-local origin port configured by the test.
    :param _ready_file: Test-local ready file configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "origin",
        ("bash", TEST_DIRECTORY / "server.sh", str(_origin_port), _ready_file),
    )


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Set the outbound connect timeout and inbound request limit.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.net.max_requests_in": 1000,
            "proxy.config.http.connect_attempts_timeout": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|socket|v_net_queue",
        })
    ats.remap_config.add_line(f"map / https://127.0.0.1:{_origin_port}/")
    return ats


def wait_for_origin(*, _origin: ProcessService, _ready_file: Path) -> None:
    """Wait for the server script to reach its netcat listener.

    :param _origin: Test-local origin configured by the test.
    :param _ready_file: Test-local ready file configured by the test.
    """

    deadline = time.monotonic() + 10
    while not _ready_file.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert _ready_file.exists(), _origin.output


def test_post_slow_server_max_requests_in(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A stalled origin handshake does not trigger the inbound request limit.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin_port = services.allocate_port()
    _ready_file = ats_factory.run_directory / "origin.ready"
    _origin = configure_origin(services, _origin_port=_origin_port, _ready_file=_ready_file)
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)

    _origin.start()
    wait_for_origin(_origin=_origin, _ready_file=_ready_file)
    _ats.start()
    result = curl.run_for(
        _ats,
        (f"--request POST --http1.1 --verbose --silent 'http://127.0.0.1:{_ats.http_port}/' --data "
         f"key=value"),
        timeout=10,
    )
    assert result.returncode == 0, result.output
    assert_matches_gold(result.stdout, TEST_DIRECTORY / "gold" / "post_slow_server_max_requests_in_0_stdout.gold")
    assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / "post_slow_server_max_requests_in_0_stderr.gold")
