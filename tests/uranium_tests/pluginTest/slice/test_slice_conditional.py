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

import pytest
import shlex

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SLICE_BLOCK_SIZE = 10
LARGE_BODY = "large object sliced!"


class ConditionalSliceScenario:
    """Slice only objects larger than the configured minimum size."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create small, unsliced-large, and ranged-large responses."""

        origin = services.origin("server", lookup_key="{PATH}{%Range}", options={"-v": None})
        origin.add_response(
            {"headers": "GET /small HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
                "body": "smol",
            },
        )
        origin.add_response(
            {"headers": "GET /large HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
                "body": "unsliced large object!",
            },
        )
        for begin in range(0, len(LARGE_BODY), SLICE_BLOCK_SIZE):
            requested_end = begin + SLICE_BLOCK_SIZE - 1
            actual_end = min(begin + SLICE_BLOCK_SIZE, len(LARGE_BODY))
            origin.add_response(
                {"headers": "GET /large HTTP/1.1\r\n"
                            "Host: www.example.com\r\n"
                            f"Range: bytes={begin}-{requested_end}\r\n\r\n"},
                {
                    "headers":
                        "HTTP/1.1 206 Partial Content\r\n"
                        "Connection: close\r\n"
                        "Accept-Ranges: bytes\r\n"
                        f"Content-Range: bytes {begin}-{actual_end - 1}/{len(LARGE_BODY)}\r\n"
                        "Cache-Control: max-age=10,public\r\n\r\n",
                    "body": LARGE_BODY[begin:actual_end],
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure conditional slicing with cache range support."""

        ats = ats_factory.create("ts")
        required = ("slice.so", "cache_range_requests.so", "xdebug.so")
        missing = [plugin for plugin in required if not ats.plugin_exists(plugin)]
        if missing:
            pytest.skip("Missing plugins: " + ", ".join(missing))
        ats.remap_config.add_line(
            f"map http://slice/ http://127.0.0.1:{self._origin.port}/ "
            f"@plugin=slice.so @pparam=--blockbytes-test={SLICE_BLOCK_SIZE} "
            "@pparam=--minimum-size=8 @pparam=--metadata-cache-size=4 "
            "@plugin=cache_range_requests.so")
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http|cache|slice|xdebug|cache_range_requests",
            })
        return ats

    def request(self, path: str, *, byte_range: str | None = None) -> str:
        """Request one object and include response headers in the result."""

        arguments = [
            "--silent",
            "--include",
            "--proxy",
            f"localhost:{self._ats.http_port}",
            "--header",
            "x-debug: x-cache",
        ]
        if byte_range is not None:
            arguments.extend(("--range", byte_range))
        arguments.append(f"http://slice/{path}")
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Verify small-object caching and the large-object slice transition."""

        self._origin.start()
        self._ats.start()
        small_miss = self.request("small")
        assert "smol" in small_miss and "X-Cache: miss" in small_miss
        small_hit = self.request("small")
        assert "smol" in small_hit and "X-Cache: hit-fresh" in small_hit
        small_range = self.request("small", byte_range="1-2")
        assert "mo" in small_range and "X-Cache: hit-fresh" in small_range
        large_unsliced = self.request("large")
        assert "unsliced large object!" in large_unsliced and "X-Cache: miss" in large_unsliced
        large_sliced = self.request("large")
        assert LARGE_BODY in large_sliced and "X-Cache: miss" in large_sliced
        large_hit = self.request("large")
        assert LARGE_BODY in large_hit and "X-Cache: hit-fresh" in large_hit


def test_slice_conditional(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """slice changes behavior only after an object exceeds the minimum size."""

    ConditionalSliceScenario(ats_factory, services, curl).run()
