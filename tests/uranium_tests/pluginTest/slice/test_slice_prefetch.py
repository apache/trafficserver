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
"""Verify slice background prefetching and its cache-state log."""

import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


class SlicePrefetchScenario:
    """Configure two slice sizes with different prefetch counts."""

    BODY = "lets go surfin now"
    BLOCK_SIZES = (7, 5)

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create full and block-range responses keyed by Range."""

        origin = services.origin("origin", lookup_key="{%Range}")
        origin.add_response(
            {"headers": "GET /path HTTP/1.1\r\nHost: origin\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: public, max-age=5\r\n\r\n",
                "body": cls.BODY,
            },
        )
        length = len(cls.BODY)
        for block_size in cls.BLOCK_SIZES:
            for index in range(length // block_size + 1):
                begin = index * block_size
                requested_end = begin + block_size - 1
                end = min(requested_end, length - 1)
                origin.add_response(
                    {
                        "headers":
                            ("GET /path HTTP/1.1\r\nHost: *\r\nAccept: */*\r\n"
                             f"Range: bytes={begin}-{requested_end}\r\n\r\n")
                    },
                    {
                        "headers":
                            (
                                "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n"
                                "Cache-Control: public, max-age=5\r\n"
                                f"Content-Range: bytes {begin}-{end}/{length}\r\nConnection: close\r\n\r\n"),
                        "body": cls.BODY[begin:end + 1],
                    },
                )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure prefetch count one and three mappings plus cache logging."""

        ats = ats_factory.create("ats")
        required = ("slice.so", "cache_range_requests.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")
        ats.remap_config.add_lines(
            (
                f"map http://sliceprefetchbytes1/ http://127.0.0.1:{self._origin.port} "
                "@plugin=slice.so @pparam=--blockbytes-test=7 @pparam=--prefetch-count=1 "
                "@plugin=cache_range_requests.so",
                f"map http://sliceprefetchbytes2/ http://127.0.0.1:{self._origin.port} "
                "@plugin=slice.so @pparam=--blockbytes-test=5 @pparam=--prefetch-count=3 "
                "@plugin=cache_range_requests.so",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "cache",
                            "format": "%<{Content-Range}psh> %<{X-Cache}psh>"
                        }],
                        "logs": [{
                            "filename": "cache",
                            "format": "cache",
                            "mode": "ascii"
                        }],
                    }
            })
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "slice|cache_range_requests|xdebug",
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        return ats

    def request(self, host: str, *, byte_range: str | None = None) -> str:
        """Request a sliced resource and return headers plus body."""

        arguments = [
            "--silent",
            "--dump-header",
            "-",
            "--header",
            "x-debug: x-cache",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
        ]
        if byte_range is not None:
            arguments.extend(("--range", byte_range))
        arguments.append(f"http://{host}/path")
        result = self._curl.run_for(self._ats, *arguments)
        assert result.returncode == 0, result.output
        return result.stdout

    @staticmethod
    def assert_response(output: str, status: str, body: str, cache: str, content_range: str | None = None) -> None:
        """Verify status, assembled body, cache state, and optional range."""

        assert status in output, output
        assert body in output, output
        assert f"X-Cache: {cache}" in output, output
        if content_range is not None:
            assert f"Content-Range: {content_range}" in output, output

    def assert_cache_log(self) -> None:
        """Verify foreground and background range cache lookups."""

        expected = (
            "bytes 0-6/18 miss",
            "bytes 7-13/18 miss",
            "bytes 14-17/18 miss",
            "bytes 14-17/18 hit-fresh",
            "- miss, none",
            "bytes 0-6/18 hit-fresh",
            "bytes 7-13/18 hit-fresh",
            "bytes 14-17/18 hit-fresh",
            "- hit-fresh, none",
            "bytes 0-6/18 hit-stale",
            "bytes 7-13/18 hit-stale",
            "bytes 14-17/18 hit-stale",
            "- hit-stale, none",
            "bytes 0-17/18 hit-fresh, none",
            "bytes 0-4/18 miss",
            "bytes 5-9/18 miss",
            "bytes 10-14/18 miss",
            "bytes 15-17/18 miss",
            "bytes 10-14/18 hit-fresh",
            "bytes 15-17/18 hit-fresh",
            "bytes 5-16/18 miss, none",
            "*/18 hit-fresh, none",
        )
        cache_log = self._ats.log_directory / "cache.log"
        content = wait_for_file_lines(cache_log, r"^\*/18 hit-fresh, none$", 1, timeout=15)
        for entry in expected:
            assert re.search(f"^{re.escape(entry)}$", content, re.MULTILINE), entry

    def run(self) -> None:
        """Exercise fresh, stale, ranged, prefetched, and invalid requests."""

        self._origin.start()
        self._ats.start()
        self.assert_response(self.request("sliceprefetchbytes1"), "200 OK", self.BODY, "miss")
        time.sleep(1)
        self.assert_response(self.request("sliceprefetchbytes1"), "200 OK", self.BODY, "hit-fresh")
        time.sleep(5)
        self.assert_response(self.request("sliceprefetchbytes1"), "200 OK", self.BODY, "hit-stale")
        self.assert_response(
            self.request("sliceprefetchbytes1", byte_range="0-"),
            "206 Partial Content",
            self.BODY,
            "hit-fresh",
            "bytes 0-17/18",
        )
        self.assert_response(
            self.request("sliceprefetchbytes2", byte_range="5-16"),
            "206 Partial Content",
            self.BODY[5:17],
            "miss",
            "bytes 5-16/18",
        )
        invalid = self.request("sliceprefetchbytes1", byte_range="19-26")
        assert "416 Requested Range Not Satisfiable" in invalid, invalid
        self.assert_cache_log()


def test_slice_prefetch(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Prefetch fills only the configured number of future slice blocks."""

    SlicePrefetchScenario(ats_factory, services).run()
