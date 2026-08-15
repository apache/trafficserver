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


class Sigusr2LogRotationScenario:
    """Exercise external rotation of system and configured logs."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    @staticmethod
    def configure_ats(ats_factory: ATSFactory, name: str) -> ATS:
        """Disable internal rolling and shorten log flush intervals."""

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

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create responses for traffic written around a rotation."""

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

    def check_system_log(self) -> None:
        """Move diags.log, signal ATS, and verify the descriptor is reseated."""

        ats = self.configure_ats(self._ats_factory, "sigusr2_ts1")
        ats.start()
        wait_for_file_lines(ats.diags_log, "traffic server running", 1, timeout=60)
        rotated = Path(f"{ats.diags_log}_old")
        ats.diags_log.replace(rotated)
        ats.send_signal(signal.SIGUSR2)
        current = wait_for_file_lines(ats.diags_log, "Reseated diags.log", 1, timeout=60)
        previous = rotated.read_text(errors="replace")
        assert "traffic server running" not in current
        assert "traffic server running" in previous

    def request(self, ats: ATS, path: str) -> None:
        """Issue one request that must be written to the configured log."""

        result = self._curl.run_for(ats, "--fail", "--silent", f"http://127.0.0.1:{ats.http_port}{path}")
        assert result.returncode == 0, result.output

    def check_configured_log(self) -> None:
        """Verify the active access log moves from an old inode to a new file."""

        origin = self.configure_origin(self._services)
        ats = self.configure_ats(self._ats_factory, "sigusr2_ts2")
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
        self.request(ats, "/first")
        wait_for_file_lines(configured, "/first", 1, timeout=60)
        configured.replace(rotated)
        self.request(ats, "/second")
        wait_for_file_lines(rotated, "/second", 1, timeout=60)
        ats.send_signal(signal.SIGUSR2)
        self.request(ats, "/third")
        current = wait_for_file_lines(configured, "/third", 1, timeout=60)
        previous = rotated.read_text(errors="replace")

        assert "/first" not in current
        assert "/second" not in current
        assert "/third" in current
        assert "/first" in previous
        assert "/second" in previous
        assert "/third" not in previous

    def run(self) -> None:
        """Run system and access-log rotation scenarios."""

        self.check_system_log()
        self.check_configured_log()


def test_sigusr2(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """SIGUSR2 reseats both diagnostics and configured log files."""

    Sigusr2LogRotationScenario(ats_factory, services, curl).run()
