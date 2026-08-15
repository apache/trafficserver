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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class CrashLogScenario:
    """Crash ATS deliberately and verify traffic_crashlog's thread report."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the origin used to establish a healthy baseline request."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n",
                "body": "Hello",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load the intentional crash plugin and enable the crash-log helper."""

        ats = ats_factory.create("ts", return_code=-11, enable_cache=False)
        ats.records.update(
            {
                "proxy.config.proxy_name": "test_proxy",
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "crash_test",
                "proxy.config.crash_log_helper": "traffic_crashlog",
            })
        ats.copy_custom_plugin("{AtsBuildUraniumTestsDir}/pluginTest/crash_test/.libs/crash_test.so")
        ats.plugin_config.add_line("crash_test.so")
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        return ats

    def verify_healthy_request(self) -> None:
        """Prove ATS is serving traffic before triggering the fault."""

        response = self._curl.get(
            self._ats,
            headers={"Host": "example.com"},
            options=("--silent", "--output", "/dev/null", "--write-out", "%{http_code}"),
        )
        assert response.returncode == 0, response.output
        assert response.stdout == "200"

    def trigger_crash(self) -> None:
        """Send the header that makes crash_test dereference a null pointer."""

        response = self._curl.get(
            self._ats,
            headers={
                "Host": "example.com",
                "X-Crash-Test": "now"
            },
            options=("--silent", "--output", "/dev/null"),
        )
        assert response.returncode in (52, 56), response.output
        self._ats.wait()

    def wait_for_crash_log(self) -> Path:
        """Wait until traffic_crashlog has finished writing its report."""

        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            matches = list(self._ats.log_directory.glob("crash-*.log"))
            if matches and "Other Non-Crashing Threads:" in matches[0].read_text(errors="replace"):
                return matches[0]
            time.sleep(0.1)
        raise AssertionError("traffic_crashlog did not produce a complete crash report")

    def run(self) -> None:
        """Exercise the healthy and crashing transactions, then inspect the report."""

        self._origin.start()
        self._ats.start()
        self.verify_healthy_request()
        self.trigger_crash()
        diagnostics = self._ats.diags_log.read_text(errors="replace")
        assert "Received crash trigger header - crashing now!" in diagnostics
        assert "This should never be reached." not in diagnostics
        crash_log = self.wait_for_crash_log().read_text(errors="replace")
        assert "Segmentation fault" in crash_log
        assert "Crashing Thread" in crash_log
        assert "Other Non-Crashing Threads:" in crash_log


def test_crash_test(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An ATS crash produces a complete, ordered crash-log backtrace."""

    CrashLogScenario(ats_factory, services, curl).run()
