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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, wait_for_file_lines


def configure_ats(
        suffix: str, system_destination: str, custom_destination: str, *, _ats_factory: ATSFactory,
        _services: ServiceFactory) -> ATS:
    """Configure system logs, a sentinel log, and one custom log.

    :param suffix: Unique suffix for the Traffic Server process name.
    :param system_destination: Filename or stream for diagnostics and
        errors.
    :param custom_destination: Filename or stream for the custom access
        log.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _services: Test-local services configured by the test.
    """

    ats = _ats_factory.create(
        f"ts-{suffix}",
        disable_log_checks=system_destination in ("stdout", "stderr"),
        capture_traffic_out=system_destination not in ("stdout", "stderr"),
    )
    closed_port = _services.allocate_port()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "log",
            "proxy.config.log.periodic_tasks_interval": 1,
            "proxy.config.diags.logfile.filename": system_destination,
            "proxy.config.error.logfile.filename":
                (system_destination.replace("diags", "error") if system_destination.endswith(".log") else system_destination),
        })
    ats.remap_config.add_lines(
        (
            f"map /server/down http://127.0.0.1:{closed_port}",
            "map / https://trafficserver.apache.org @action=deny",
        ))
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "url_and_return_code",
                        "format": "%<pqu>: %<pssc>"
                    }],
                    "logs":
                        [
                            {
                                "filename": "sentinel",
                                "format": "url_and_return_code"
                            },
                            {
                                "filename": custom_destination,
                                "format": "url_and_return_code"
                            },
                        ],
                }
        })
    return ats


def send_traffic(ats: ATS, *, _curl: Curl) -> None:
    """Generate one denied transaction and one failed origin connection.

    :param ats: Traffic Server instance that receives the transactions.

    :param _curl: Test-local curl configured by the test.
    """

    ats.start()
    result = _curl.run_for(
        ats,
        (
            f"'http://127.0.0.1:{ats.http_port}/some/path' --verbose --next "
            f"'http://127.0.0.1:{ats.http_port}/server/down' --verbose"),
    )
    assert result.returncode == 0, result.output
    wait_for_file_lines(ats.log_directory / "sentinel.log", r"^http://127\.0\.0\.1:\d+/: 502$", 1)


def assert_logs(ats: ATS, system_destination: str, custom_destination: str) -> None:
    """Verify expected system diagnostics and access entries.

    :param ats: Traffic Server instance whose output is inspected.
    :param system_destination: Filename or stream receiving diagnostics
        and errors.
    :param custom_destination: Filename or stream receiving the custom
        access log.
    """

    if system_destination in ("stdout", "stderr"):
        system_content = ats.process_output
        error_content = system_content
    else:
        system_content = wait_for_file_lines(
            ats.log_directory / system_destination,
            "logging.yaml finished loading",
            1,
        )
        error_filename = system_destination.replace("diags", "error")
        error_content = wait_for_file_lines(
            ats.log_directory / error_filename,
            "CONNECT: attempt fail",
            1,
        )

    if custom_destination in ("stdout", "stderr"):
        custom_content = ats.process_output
    else:
        custom_content = wait_for_file_lines(
            ats.log_directory / f"{custom_destination}.log",
            "https://trafficserver.apache.org/some/path: 403",
            1,
        )

    assert "logging.yaml finished loading" in system_content
    assert "CONNECT: attempt fail" in error_content
    assert "https://trafficserver.apache.org/some/path: 403" in custom_content


def run_case(
        suffix: str, system_destination: str, custom_destination: str, *, _ats_factory: ATSFactory, _curl: Curl,
        _services: ServiceFactory) -> None:
    """Run and verify one destination combination.

    :param suffix: Unique suffix for the Traffic Server process name.
    :param system_destination: Filename or stream receiving diagnostics
        and errors.
    :param custom_destination: Filename or stream receiving the custom
        access log.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    ats = configure_ats(suffix, system_destination, custom_destination, _ats_factory=_ats_factory, _services=_services)
    send_traffic(ats, _curl=_curl)
    if system_destination in ("stdout", "stderr"):
        ats.stop()
    assert_logs(ats, system_destination, custom_destination)


def test_log_filenames(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS writes system and custom logs to configured files or streams.

    :param ats_factory: Fixture that owns the Traffic Server processes.
    :param services: Fixture used to reserve closed listener ports.
    :param curl: Fixture that sends curl requests to Traffic Server.
    """

    run_case("default", "diags.log", "my_custom_log", _ats_factory=ats_factory, _curl=curl, _services=services)
    run_case("renamed", "my_diags.log", "my_custom_log", _ats_factory=ats_factory, _curl=curl, _services=services)
    run_case("stdout", "stdout", "stdout", _ats_factory=ats_factory, _curl=curl, _services=services)
    run_case("stderr", "stderr", "stderr", _ats_factory=ats_factory, _curl=curl, _services=services)
