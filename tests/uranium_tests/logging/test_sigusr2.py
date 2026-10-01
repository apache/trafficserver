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
import signal

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


def configure_ats(ats_factory: ATSFactory, name: str) -> ATS:
    """Disable internal rolling and shorten log flush intervals.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    """

    ats = ats_factory.create(name)
    ats.records.update(
        {
            "proxy.config.http.wait_for_cache": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "log",
            "proxy.config.log.periodic_tasks_interval": 1,
            "proxy.config.log.rolling_enabled": 0,
            "proxy.config.log.auto_delete_rolled_files": 0,
        })
    return ats


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create responses for traffic written around a rotation.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("sigusr2_server")
    for path in ("/first", "/second", "/third"):
        origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                "body": "xxx",
            },
        )
    return origin


def check_system_log(*, _ats_factory: ATSFactory) -> None:
    """Move diags.log, signal ATS, and verify the descriptor is reseated.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = configure_ats(_ats_factory, "sigusr2_ts1")
    ats.start()
    wait_for_file_lines(ats.diags_log, "traffic server running", 1, timeout=60)
    rotated = Path(f"{ats.diags_log}_old")
    ats.diags_log.replace(rotated)
    ats.send_signal(signal.SIGUSR2)
    current = wait_for_file_lines(ats.diags_log, "Reseated diags.log", 1, timeout=60)
    previous = rotated.read_text(errors="replace")
    assert "traffic server running" not in current
    assert "traffic server running" in previous


def request(ats: ATS, path: str, *, _curl: Curl) -> None:
    """Issue one request that must be written to the configured log.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param path: Resource or file path used by this operation.
    """

    result = _curl.run_for(
        ats,
        f"--fail --silent 'http://127.0.0.1:{ats.http_port}{path}'",
    )
    assert result.returncode == 0, result.output


def check_configured_log(*, _ats_factory: ATSFactory, _curl: Curl, _services: ServiceFactory) -> None:
    """Verify the active access log moves from an old inode to a new file.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    origin = configure_origin(_services)
    ats = configure_ats(_ats_factory, "sigusr2_ts2")
    ats.remap_config.add_line(f"map http://127.0.0.1:{ats.http_port} http://127.0.0.1:{origin.http_port}")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "has_path",
                        "format": "%<pqu>: %<sssc>"
                    }],
                    "logs": [{
                        "filename": "test_rotation",
                        "format": "has_path"
                    }],
                }
        })
    configured = ats.log_directory / "test_rotation.log"
    rotated = Path(f"{configured}_old")

    origin.start()
    ats.start()
    request(ats, "/first", _curl=_curl)
    wait_for_file_lines(configured, "/first", 1, timeout=60)
    configured.replace(rotated)
    request(ats, "/second", _curl=_curl)
    wait_for_file_lines(rotated, "/second", 1, timeout=60)
    ats.send_signal(signal.SIGUSR2)
    request(ats, "/third", _curl=_curl)
    current = wait_for_file_lines(configured, "/third", 1, timeout=60)
    previous = rotated.read_text(errors="replace")

    assert "/first" not in current
    assert "/second" not in current
    assert "/third" in current
    assert "/first" in previous
    assert "/second" in previous
    assert "/third" not in previous


def test_sigusr2(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SIGUSR2 reseats both diagnostics and configured log files.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    check_system_log(_ats_factory=ats_factory)
    check_configured_log(_ats_factory=ats_factory, _curl=curl, _services=services)
