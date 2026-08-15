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
"""Verify slice reports inconsistent or missing internal blocks."""

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


class SliceErrorScenario:
    """Create four malformed block sequences and inspect slice diagnostics."""

    BODY = "the quick brown fox"
    BLOCK_BYTES = 9

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @classmethod
    def add_block(
        cls,
        origin: OriginServer,
        path: str,
        index: int,
        *,
        etag: str | None = None,
        last_modified: str | None = None,
        total_length: int = 19,
        status: str = "206 Partial Content",
    ) -> None:
        """Add one internal range response for a malformed object."""

        begin = index * cls.BLOCK_BYTES
        end = begin + cls.BLOCK_BYTES - 1
        body = cls.BODY[begin:end + 1]
        fields = [f"HTTP/1.1 {status}", "Connection: close"]
        if etag is not None:
            fields.append(f"Etag: {etag}")
        if last_modified is not None:
            fields.append(f"Last-Modified: {last_modified}")
        if status.startswith("206"):
            fields.extend((f"Content-Range: bytes {begin}-{end}/{total_length}", "Cache-Control: max-age=500"))
        origin.add_response(
            {
                "headers":
                    (
                        f"GET /{path} HTTP/1.1\r\nHost: ats\r\nRange: bytes={begin}-{end}\r\n"
                        "X-Slicer-Info: full content request\r\n\r\n")
            },
            {
                "headers": "\r\n".join(fields) + "\r\n\r\n",
                "body": body
            },
        )

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create ETag, Last-Modified, Content-Range, and 404 failures."""

        origin = services.origin("origin", lookup_key="{%Range}{PATH}")
        cls.add_block(origin, "etag", 0, etag='"etag0"')
        cls.add_block(origin, "etag", 1, etag='"etag1"')
        cls.add_block(origin, "lastmodified", 0, last_modified="Tue, 08 May 2018 15:49:41 GMT")
        cls.add_block(origin, "lastmodified", 1, last_modified="Tue, 08 Apr 2019 18:00:00 GMT")
        cls.add_block(origin, "crr", 0, etag="crr")
        cls.add_block(origin, "crr", 1, etag="crr", total_length=18)
        cls.add_block(
            origin,
            "internal404",
            0,
            etag='"etag"',
            last_modified="Tue, 08 May 2018 15:49:41 GMT",
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure slice with nine-byte test blocks and no cache."""

        ats = ats_factory.create("ats", enable_cache=False)
        if not ats.plugin_exists("slice.so"):
            pytest.skip("slice.so is not installed")
        ats.remap_config.add_line(
            f"map / http://127.0.0.1:{self._origin.port} @plugin=slice.so @pparam=--blockbytes-test={self.BLOCK_BYTES}")
        return ats

    def request(self, path: str) -> str:
        """Request one malformed object and return headers plus partial body."""

        result = self._curl.get(
            self._ats,
            f"/{path}",
            headers={"Host": "ats"},
            options=("--silent", "--show-error", "--dump-header", "-"),
        )
        assert "HTTP/1.1 200 OK" in result.stdout, result.output
        assert self.BODY[:self.BLOCK_BYTES] in result.stdout, result.output
        return result.output

    def run(self) -> None:
        """Issue each malformed sequence and wait for its diagnostic."""

        self._origin.start()
        self._ats.start()
        cases = (
            ("etag", "Mismatch block Etag"),
            ("lastmodified", "Mismatch block Last-Modified"),
            ("crr", "Mismatch/Bad block Content-Range"),
            ("internal404", "404 internal block response"),
        )
        for path, diagnostic in cases:
            self.request(path)
            wait_for_file_lines(self._ats.diags_log, diagnostic, 1)


def test_slice_error(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Slice logs the reason it aborts an inconsistent block stream."""

    SliceErrorScenario(ats_factory, services).run()
