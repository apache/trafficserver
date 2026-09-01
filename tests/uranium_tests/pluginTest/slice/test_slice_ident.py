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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
LAST_MODIFIED = "Fri, 07 Mar 2025 18:06:58 GMT"


class SliceIdentScenario:
    """Verify the validator identity header used by slice subrequests."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("slice.so"):
            pytest.skip("slice.so is required")

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create otherwise identical objects with ETag and Last-Modified validators."""

        origin = services.origin("server")
        common = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=500\r\n"
        origin.add_response(
            {"headers": "GET /etag HTTP/1.1\r\nHost: origin\r\n\r\n"},
            {
                "headers": common + f'Etag: "foo"\r\nLast-Modified: {LAST_MODIFIED}\r\n\r\n',
                "body": "lets go surfin now"
            },
        )
        origin.add_response(
            {"headers": "GET /lm HTTP/1.1\r\nHost: origin\r\n\r\n"},
            {
                "headers": common + f"Last-Modified: {LAST_MODIFIED}\r\n\r\n",
                "body": "lets go surfin now"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure default and custom slice identity header names."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "slice",
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        plugin = " @plugin=slice.so @pparam=--blockbytes-test=11"
        ats.remap_config.add_lines(
            (
                f"map http://preload/ http://127.0.0.1:{self._origin.port}",
                f"map http://slice/ http://127.0.0.1:{self._origin.port}{plugin}",
                f"map http://slicecustom/ http://127.0.0.1:{self._origin.port}{plugin} @pparam=--crr-ident-header=CrrIdent",
            ))
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
                                            "%<cquup> %<sssc> %<pssc> range=::%<{Range}cqh>:: "
                                            "x-crr-ident=::%<{X-Crr-Ident}cqh>:: crrident=::%<{CrrIdent}cqh>::"),
                                }
                            ],
                        "logs": [{
                            "filename": "transaction",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def request(self, host: str, path: str, expected_status: str) -> None:
        """Fetch one object and verify its client-facing status."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--silent --dump-header /dev/stdout --output /dev/null --proxy 'localhost:{self._ats.http_port}' "
                f"'http://{host}/{path}'"),
        )
        assert result.returncode == 0, result.output
        assert expected_status in result.stdout

    def run(self) -> None:
        """Generate default and custom identity subrequests and validate the log."""

        self._origin.start()
        self._ats.start()
        for path in ("etag", "lm"):
            self.request("preload", path, "200 OK")
        self.request("slice", "etag", "200 OK")
        self.request("slicecustom", "etag", "200 OK")
        self.request("slice", "lm", "200 OK")
        self.request("slicecustom", "lm", "200 OK")
        self.request("prefetch", "404.txt", "404")
        log_path = self._ats.log_directory / "transaction.log"
        content = wait_for_file_lines(log_path, r"404\.txt", 1)
        assert_matches_gold(content, TEST_DIRECTORY / "gold" / "slice_ident.gold")


def test_slice_ident(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Slice subrequests identify their validators with the configured header name."""

    SliceIdentScenario(ats_factory, services, curl).run()
