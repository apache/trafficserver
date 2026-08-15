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
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class SliceContentShrinkScenario:
    """Make content shrink below the byte range selected by the slice plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin_port = services.allocate_port()
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> ProcessService:
        """Start the origin that changes length and ETag between slice fetches."""

        return services.process(
            "origin",
            (sys.executable, TEST_DIRECTORY / "shrink_origin.py", str(self._origin_port)),
            ready_port=self._origin_port,
        )

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the slice plugin with seven-byte test blocks."""

        ats = ats_factory.create("ts", enable_cache=False)
        if not ats.plugin_exists("slice.so"):
            pytest.skip("slice.so is required")
        ats.remap_config.add_line(
            f"map http://slice/ http://127.0.0.1:{self._origin_port}/ "
            "@plugin=slice.so @pparam=--blockbytes-test=7")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "slice",
        })
        return ats

    def request_range(self, path: str, byte_range: str) -> CommandResult:
        """Request one range through ATS's forward-proxy listener."""

        return self._curl.run_for(
            self._ats,
            "--silent",
            "--dump-header",
            "/dev/stdout",
            "--output",
            "/dev/stderr",
            "--proxy",
            f"localhost:{self._ats.http_port}",
            f"http://slice/{path}",
            "--range",
            byte_range,
            "--write-out",
            "\nSIZE:%{size_download}",
        )

    @staticmethod
    def verify_empty_response(result: CommandResult) -> None:
        """Require the failed range to expose no response body."""

        assert result.returncode in (0, 18), result.output
        assert re.search(r"SIZE:0\b", result.stdout)
        assert result.stderr == ""

    def run(self) -> None:
        """Exercise aligned and mid-block shrink cases."""

        self._origin.start()
        self._ats.start()
        self.verify_empty_response(self.request_range("shrink", "14-20"))
        second = self.request_range("shrink_mid", "16-20")
        assert second.returncode in (0, 18), second.output
        wait_for_file_lines(self._ats.diags_log, "shrunk below requested range start", 1)


def test_slice_content_shrink(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Shrinking content fails cleanly without an unsigned slice-offset underflow."""

    SliceContentShrinkScenario(ats_factory, services, curl).run()
