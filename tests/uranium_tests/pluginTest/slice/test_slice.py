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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent
BODY = "lets go surfin now"


class SliceScenario:
    """Verify basic slice-plugin range assembly from a cached complete object."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("slice.so"):
            pytest.skip("slice.so is required")

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the cacheable object split into seven-byte blocks by the plugin."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET /path HTTP/1.1\r\nHost: origin\r\n\r\n"},
            {
                "headers": ('HTTP/1.1 200 OK\r\nConnection: close\r\nEtag: "path"\r\n'
                            "Cache-Control: max-age=500\r\n\r\n"),
                "body": BODY,
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure preload, sliced, and skip-header remap targets."""

        ats = ats_factory.create("ts")
        ats.records.update({"proxy.config.diags.debug.enabled": 0, "proxy.config.diags.debug.tags": "slice"})
        plugin = " @plugin=slice.so @pparam=--blockbytes-test=7"
        ats.remap_config.add_lines(
            (
                f"map http://preload/ http://127.0.0.1:{self._origin.port}",
                f"map http://slice_only/ http://127.0.0.1:{self._origin.port}",
                f"map http://slice/ http://127.0.0.1:{self._origin.port}{plugin}",
                f"map http://slicehdr/ http://127.0.0.1:{self._origin.port}{plugin} @pparam=--skip-header=SkipSlice",
            ))
        return ats

    def request(self, host: str, byte_range: str | None = None) -> CommandResult:
        """Fetch one object while separating headers from its response body."""

        arguments = [
            "--silent",
            "--dump-header",
            "/dev/stdout",
            "--output",
            "/dev/stderr",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
        ]
        if byte_range is not None:
            arguments.extend(("--range", byte_range))
        arguments.append(f"http://{host}/path")
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        return result

    @staticmethod
    def assert_response(
        result: CommandResult,
        status: str,
        body_gold: str | None = None,
        content_range: str | None = None,
    ) -> None:
        """Validate one response status, range, and exact body."""

        assert status in result.stdout
        if content_range is not None:
            assert content_range in result.stdout
        if body_gold is not None:
            assert_matches_gold(result.stderr, TEST_DIRECTORY / "gold" / body_gold)

    def run(self) -> None:
        """Run the complete, aligned, truncated, and unsatisfiable range matrix."""

        self._origin.start()
        self._ats.start()
        self.assert_response(self.request("preload"), "200 OK", "slice_200.stderr.gold")
        cases = (
            ("0-6", "slice_first.stderr.gold", "Content-Range: bytes 0-6/18"),
            ("14-", "slice_last.stderr.gold", "Content-Range: bytes 14-17/18"),
            ("14-17", "slice_last.stderr.gold", "Content-Range: bytes 14-17/18"),
            ("14-20", "slice_last.stderr.gold", "Content-Range: bytes 14-17/18"),
            ("0-", "slice_206.stderr.gold", "Content-Range: bytes 0-17/18"),
            ("5-16", "slice_mid.stderr.gold", "Content-Range: bytes 5-16/18"),
        )
        for byte_range, body_gold, content_range in cases[:4]:
            self.assert_response(self.request("slice", byte_range), "206 Partial Content", body_gold, content_range)
        self.assert_response(self.request("slice"), "200 OK", "slice_200.stderr.gold")
        for byte_range, body_gold, content_range in cases[4:]:
            self.assert_response(self.request("slice", byte_range), "206 Partial Content", body_gold, content_range)
        invalid_begin = len(BODY) + 1
        self.assert_response(self.request("slice", f"{invalid_begin}-{invalid_begin + 7}"), "416 Requested Range Not Satisfiable")
        self.assert_response(
            self.request("slicehdr", "0-6"),
            "206 Partial Content",
            "slice_first.stderr.gold",
            "Content-Range: bytes 0-6/18",
        )


def test_slice(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The slice plugin reconstructs complete and partial responses correctly."""

    SliceScenario(ats_factory, services, curl).run()
