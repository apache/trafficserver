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

from collections.abc import Mapping, Sequence
import re
import socket
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


@pytest.mark.manual(reason="sensitive to timing and known to be flaky")
@pytest.mark.serial
def test_log_retention(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS enforces log-space retention, priorities, and roll limits.

    :param ats_factory: Factory that owns each phase's ATS instance.
    :param services: Factory that owns the shared origin process.
    :param curl: Curl client used to generate log entries.
    """

    __base_records = {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "logspace",
        "proxy.config.log.rolling_enabled": 3,
        "proxy.config.log.auto_delete_rolled_files": 1,
        "proxy.config.log.rolling_size_mb": 10,
        "proxy.config.log.periodic_tasks_interval": 1,
        "proxy.config.log.max_secs_per_buffer": 1,
    }
    __long_prefix = "0123456789" * 500

    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the shared cacheable origin for every retention phase.

        :param services: Factory that owns the origin process.
        """

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                "body": "xxx",
            },
        )
        return origin

    def configure_ats(
            records: Mapping[str, object],
            logs: Sequence[Mapping[str, object]] = (),
            *,
            plugin: bool = False,
    ) -> ATS:
        """Create one isolated ATS instance for a retention phase.

        :param records: Record overrides for this phase.
        :param logs: Custom access-log definitions for this phase.
        :param plugin: Whether to load the log-interface test plugin.
        """
        nonlocal _counter

        ats = ats_factory.create(f"ts{_counter}")
        _counter += 1
        combined = dict(__base_records)
        combined.update(records)
        ats.records.update(combined)
        ats.remap_config.add_line(f"map http://127.0.0.1:{ats.http_port} http://127.0.0.1:{_origin.port}")
        if logs:
            ats.set_logging_yaml(
                {"logging": {
                    "formats": [{
                        "name": "long",
                        "format": f"{__long_prefix}: %<sssc>",
                    }],
                    "logs": list(logs),
                }})
        if plugin:
            ats.copy_custom_plugin("{AtsTestPluginsDir}/test_log_interface.so")
            ats.plugin_config.add_line("test_log_interface.so")
        return ats

    def send_requests(ats: ATS, count: int) -> None:
        """Generate enough 5 KB access-log entries to force rolling.

        :param ats: ATS instance that receives the requests.
        :param count: Number of requests to send.
        """

        script = (
            f"for ((request = 0; request < {count}; ++request)); do "
            f'{{curl}} --fail --silent --output /dev/null "http://127.0.0.1:{ats.http_port}/"; '
            "done")
        result = curl.run_script(ats, script, timeout=300)
        assert result.returncode == 0, result.output

    def assert_messages(ats: ATS, contains: Sequence[str], excludes: Sequence[str] = ()) -> str:
        """Wait for positive diagnostics and reject forbidden diagnostics.

        :param ats: ATS instance whose output is inspected.
        :param contains: Regular expressions that must appear.
        :param excludes: Regular expressions that must not appear.
        """

        output = ""
        for expression in contains:
            output = wait_for_file_lines(ats.traffic_out, expression, 1, timeout=30)
        if not output:
            time.sleep(2)
            output = ats.traffic_out.read_text(errors="replace")
        for expression in excludes:
            assert re.search(expression, output, re.MULTILINE) is None, output
        return output

    def wait_for_rolled_log(ats: ATS, stem: str) -> None:
        """Wait until rolling creates a timestamped file.

        :param ats: ATS instance whose log directory is inspected.
        :param stem: Base filename of the log expected to roll.
        """

        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if any(ats.log_directory.glob(f"{stem}_*")):
                return
            time.sleep(0.1)
        raise AssertionError(f"{stem} did not roll in {ats.log_directory}")

    def reload_logging(ats: ATS) -> None:
        """Touch logging.yaml and wait for a monitored configuration reload.

        :param ats: ATS instance whose logging configuration is reloaded.
        """

        logging_path = ats.config_directory / "logging.yaml"
        logging_path.touch()
        result = ats.traffic_ctl(
            "config",
            "reload",
            "-m",
            "-t",
            f"log-retention-{ats.name}",
            "-w",
            "1",
            "-r",
            "0.5",
            "-T",
            "30s",
        )
        assert result.returncode == 0, result.output

    def custom_log(filename: str, *, minimum: int | None = None) -> Mapping[str, object]:
        """Build one long-format logging.yaml entry.

        :param filename: Base filename for the custom log.
        :param minimum: Minimum number of rolled files to retain.
        """

        entry: dict[str, object] = {"filename": filename, "format": "long"}
        if minimum is not None:
            entry["rolling_min_count"] = minimum
        return entry

    def core_registrations(
        *,
        error: int = 0,
        output: int = 0,
        diagnostics: int = 0,
    ) -> tuple[str, ...]:
        """Return the expected retention registrations for ATS core logs.

        :param error: Minimum roll count for ``error.log``.
        :param output: Minimum roll count for ``traffic.out``.
        :param diagnostics: Minimum roll count for diagnostic and manager logs.
        """

        return (
            rf"Registering rotated log deletion for error\.log with min roll count {error}",
            rf"Registering rotated log deletion for traffic\.out with min roll count {output}",
            rf"Registering rotated log deletion for diags\.log with min roll count {diagnostics}",
            rf"Registering rotated log deletion for manager\.log with min roll count {diagnostics}",
        )

    _origin = configure_origin(services)
    _counter = 0

    _origin.start()

    hostname = "my_hostname"
    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 12,
            "proxy.config.log.hostname": hostname,
        },
        (custom_log("test_deletion"),),
    )
    ats.start()
    send_requests(ats, 2500)
    assert_messages(
        ats,
        (
            r"Registering rotated log deletion for test_deletion\.log with min roll count 0",
            *core_registrations(),
            rf"The rolled logfile.*test_deletion\.log_{hostname}.*was auto-deleted.*bytes were reclaimed",
        ),
    )
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 12,
            "proxy.config.log.hostname": "my_hostname",
        },
        (custom_log("test_deletion", minimum=1),),
    )
    ats.start()
    send_requests(ats, 2500)
    assert_messages(
        ats,
        (
            r"Registering rotated log deletion for test_deletion\.log with min roll count 1",
            *core_registrations(),
            r"The rolled logfile.*test_deletion\.log_my_hostname.*was auto-deleted.*bytes were reclaimed",
        ),
    )
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 12,
            "proxy.config.log.hostname": "my_hostname",
        },
        plugin=True,
    )
    ats.start()
    send_requests(ats, 2500)
    assert_messages(
        ats,
        (
            r"Registering rotated log deletion for test_log_interface\.log with min roll count 0",
            *core_registrations(),
            r"The rolled logfile.*test_log_interface\.log_.*was auto-deleted.*bytes were reclaimed",
        ),
    )
    ats.stop()

    hostname = socket.gethostname()
    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 22,
        },
        (
            custom_log("test_low_priority_deletion", minimum=5),
            custom_log("test_high_priority_deletion", minimum=1),
        ),
    )
    ats.start()
    send_requests(ats, 2500)
    assert_messages(
        ats,
        (
            r"Registering rotated log deletion for test_low_priority_deletion\.log with min roll count 5",
            r"Registering rotated log deletion for test_high_priority_deletion\.log with min roll count 1",
            *core_registrations(),
            rf"The rolled logfile.*test_high_priority_deletion\.log_{re.escape(hostname)}.*was auto-deleted",
        ),
        (r"The rolled logfile.*test_low_priority_deletion\.log_.*was auto-deleted",),
    )
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_for_logs": 22,
            "proxy.config.log.rolling_min_count": 3,
            "proxy.config.output.logfile.rolling_min_count": 4,
            "proxy.config.diags.logfile.rolling_min_count": 5,
        })
    ats.start()
    send_requests(ats, 1)
    assert_messages(
        ats,
        core_registrations(error=3, output=4, diagnostics=5),
        (r"Registering .* with min roll count 0",),
    )
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.log.auto_delete_rolled_files": 0,
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 12,
            "proxy.config.log.hostname": "my_hostname",
        },
        (custom_log("test_deletion", minimum=1),),
    )
    ats.start()
    send_requests(ats, 2500)
    wait_for_rolled_log(ats, "test_deletion.log")
    assert_messages(
        ats,
        (),
        (
            r"Registering rotated log deletion",
            r"The rolled logfile.*test_deletion\.log_.*was auto-deleted",
        ),
    )
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.diags.debug.tags": "log-file",
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 100,
            "proxy.config.log.rolling_max_count": 2,
        },
        (custom_log("test_deletion"),),
    )
    ats.start()
    send_requests(ats, 7500)
    assert_messages(ats, (r"rolled logfile.*test_deletion\.log.*old.* was auto-deleted",))
    ats.stop()

    ats = configure_ats(
        {
            "proxy.config.log.max_space_mb_headroom": 2,
            "proxy.config.log.max_space_mb_for_logs": 12,
            "proxy.config.log.hostname": "my_hostname",
        },
        (custom_log("test_deletion"),),
    )
    ats.start()
    reload_logging(ats)
    send_requests(ats, 2500)
    assert_messages(
        ats,
        (
            r"Registering rotated log deletion for test_deletion\.log with min roll count 0",
            *core_registrations(),
            r"The rolled logfile.*test_deletion\.log_.*was auto-deleted.*bytes were reclaimed",
        ),
    )
    ats.stop()
