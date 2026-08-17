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
import shlex
import subprocess
import sys
import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class AllHeadersLogScenario:
    """Verify the all-headers logging fields for an origin response and cache hit."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the cacheable origin response used by both requests."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                "body": "xxx",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the all-headers log format and cache mapping."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http|dns",
        })
        ats.remap_config.add_line(f"map http://127.0.0.1:{ats.http_port} http://127.0.0.1:{self._origin.port}")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "custom",
                                    "format":
                                        (
                                            "%<cqah> ___FS___ %<pssc> ___FS___ %<psah> ___FS___ "
                                            "%<ssah> ___FS___ %<pqah> ___FS___ %<cssah> ___FS___ END_TXN"),
                                }
                            ],
                        "logs": [{
                            "filename": "test_all_headers",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def request(self) -> None:
        """Send the long-header request through the selected transport."""

        value = "abcdefghijklmnop"
        value = value + value
        value = value + value
        value = value + value
        arguments = ["--user-agent", "007", "--verbose"]
        if self._curl.uses_uds:
            arguments.extend(("--header", f"Host: 127.0.0.1:{self._ats.http_port}"))
        for number in range(3):
            arguments.extend(("--header", f"x-header{number}: {value}"))
        arguments.append(f"http://127.0.0.1:{self._ats.http_port}")
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output

    def sanitize_log(self) -> str:
        """Apply the original deterministic sanitizers to the generated log."""

        log_path = self._ats.log_directory / "test_all_headers.log"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if log_path.exists() and len(log_path.read_text(errors="replace").splitlines()) >= 2:
                break
            time.sleep(0.1)
        python_result = subprocess.run(
            (sys.executable, TEST_DIRECTORY / "all_headers_sanitizer.py", log_path, str(self._origin.port)),
            capture_output=True,
            text=True,
            check=False,
        )
        assert python_result.returncode == 0, python_result.stderr
        shell_result = subprocess.run(
            ("sh", TEST_DIRECTORY / "all_headers_sanitizer.sh"),
            input=python_result.stdout,
            capture_output=True,
            text=True,
            check=False,
        )
        assert shell_result.returncode == 0, shell_result.stderr
        return shell_result.stdout

    def run(self) -> None:
        """Log an origin fetch and cache hit, then compare sanitized output."""

        self._origin.start()
        self._ats.start()
        self.request()
        self.request()
        gold = "test_all_headers_uds.gold" if self._curl.uses_uds else "test_all_headers.gold"
        assert_matches_gold(self.sanitize_log(), TEST_DIRECTORY / "gold" / gold)


def test_all_headers(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """All request and response header log fields remain correctly aligned."""

    AllHeadersLogScenario(ats_factory, services, curl).run()
