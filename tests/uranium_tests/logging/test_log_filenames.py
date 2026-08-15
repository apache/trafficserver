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


class LogFilenamesScenario:
    """Verify system and custom logs honor configured destinations."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    def configure_ats(self, suffix: str, system_destination: str, custom_destination: str) -> ATS:
        """Configure system logs, a sentinel log, and one custom log."""

        ats = self._ats_factory.create(
            f"ts-{suffix}",
            disable_log_checks=system_destination in ("stdout", "stderr"),
            capture_traffic_out=system_destination not in ("stdout", "stderr"),
        )
        closed_port = self._services.allocate_port()
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

    def send_traffic(self, ats: ATS) -> None:
        """Generate one denied transaction and one failed origin connection."""

        ats.start()
        result = self._curl.run_for(
            ats,
            f"http://127.0.0.1:{ats.http_port}/some/path",
            "--verbose",
            "--next",
            f"http://127.0.0.1:{ats.http_port}/server/down",
            "--verbose",
        )
        assert result.returncode == 0, result.output
        wait_for_file_lines(ats.log_directory / "sentinel.log", r"^http://127\.0\.0\.1:\d+/: 502$", 1)

    @staticmethod
    def assert_logs(ats: ATS, system_destination: str, custom_destination: str) -> None:
        """Verify expected system diagnostics and access entries."""

        if system_destination in ("stdout", "stderr"):
            system_content = ats.process_output
            error_content = system_content
        else:
            system_content = (ats.log_directory / system_destination).read_text(errors="replace")
            error_filename = system_destination.replace("diags", "error")
            error_content = (ats.log_directory / error_filename).read_text(errors="replace")

        if custom_destination in ("stdout", "stderr"):
            custom_content = ats.process_output
        else:
            custom_content = (ats.log_directory / f"{custom_destination}.log").read_text(errors="replace")

        assert "logging.yaml finished loading" in system_content
        assert "CONNECT: attempt fail" in error_content
        assert "https://trafficserver.apache.org/some/path: 403" in custom_content

    def run_case(self, suffix: str, system_destination: str, custom_destination: str) -> None:
        """Run and verify one destination combination."""

        ats = self.configure_ats(suffix, system_destination, custom_destination)
        self.send_traffic(ats)
        if system_destination in ("stdout", "stderr"):
            ats.stop()
        self.assert_logs(ats, system_destination, custom_destination)

    def run(self) -> None:
        """Exercise default, renamed, stdout, and stderr log destinations."""

        self.run_case("default", "diags.log", "my_custom_log")
        self.run_case("renamed", "my_diags.log", "my_custom_log")
        self.run_case("stdout", "stdout", "stdout")
        self.run_case("stderr", "stderr", "stderr")


def test_log_filenames(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS writes system and custom logs to configured files or streams."""

    LogFilenamesScenario(ats_factory, services, curl).run()
