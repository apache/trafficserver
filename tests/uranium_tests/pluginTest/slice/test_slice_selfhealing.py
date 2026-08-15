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
"""Verify slice repairs inconsistent cached blocks."""

from datetime import UTC, datetime, timedelta
from email.utils import format_datetime
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines


class SliceSelfHealingScenario:
    """Seed inconsistent blocks and exercise each slice repair path."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @staticmethod
    def add_range_response(origin: OriginServer, uid: str, byte_range: str, etag: str, body: str) -> None:
        """Add a cacheable five-byte asset block."""

        start, requested_end = (int(value) for value in byte_range.split("-"))
        end = min(requested_end, 4)
        origin.add_response(
            {
                "headers":
                    (f"GET {{PATH}} HTTP/1.1\r\nHost: www.example.com\r\nuuid: {uid}\r\n"
                     f"Range: bytes={byte_range}\r\n\r\n")
            },
            {
                "headers":
                    (
                        "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nCache-Control: max-age=5000\r\n"
                        f"Connection: close\r\nContent-Range: bytes {start}-{end}/5\r\nEtag: \"{etag}\"\r\n\r\n"),
                "body": body,
            },
        )

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create old, new, non-range, missing, and custom-identity responses."""

        origin = services.origin("origin", lookup_key="{%uuid}")
        for uid, byte_range, etag, body in (
            ("etagold-1", "3-5", "etagold", "aa"),
            ("etagnew-0", "0-2", "etagnew", "bbb"),
            ("etagnew-1", "3-5", "etagnew", "bb"),
            ("etagold-0", "0-2", "etagold", "aaa"),
            ("assetgone-0", "0-2", "etag", "aaa"),
            ("etagold-custom-1", "3-5", "etagold-custom", "aa"),
            ("etagnew-custom-0", "0-2", "etagnew-custom", "bbb"),
            ("etagnew-custom-1", "3-5", "etagnew-custom", "bb"),
        ):
            cls.add_range_response(origin, uid, byte_range, etag, body)
        origin.add_response(
            {"headers": ("GET /code200 HTTP/1.1\r\nHost: www.example.com\r\nuuid: code200\r\n"
                         "Range: bytes=3-5\r\n\r\n")},
            {
                "headers": ("HTTP/1.1 200 OK\r\nCache-Control: max-age=5000\r\nConnection: close\r\nEtag: \"etag\"\r\n\r\n"),
                "body": "ccccc",
            },
        )
        origin.add_response(
            {"headers": "GET {PATH} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n",
                "body": "Not Found"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure default and custom identity repair chains."""

        ats = ats_factory.create("ats")
        required = ("slice.so", "cache_range_requests.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("slice.so, cache_range_requests.so, and xdebug.so are required")
        origin = f"http://127.0.0.1:{self._origin.port}/"
        ats.remap_config.add_lines(
            (
                f"map http://slice/ {origin} @plugin=slice.so @pparam=--blockbytes-test=3 @pparam=--remap-host=crr",
                f"map http://crr/ {origin} @plugin=cache_range_requests.so @pparam=--consider-ident",
                f"map http://slicehdr/ {origin} @plugin=slice.so @pparam=--blockbytes-test=3 "
                "@pparam=--remap-host=crrhdr @pparam=--crr-ident-header=crr-foo",
                f"map http://crrhdr/ {origin} @plugin=cache_range_requests.so @pparam=--ident-header=crr-foo",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "cache_range_requests|slice",
        })
        return ats

    def request(
        self,
        host: str,
        path: str,
        *,
        byte_range: str | None = None,
        uid: str | None = None,
        identity: str | None = None,
        show_download_size: bool = False,
    ) -> CommandResult:
        """Issue one range request through the persistent ATS cache."""

        arguments = [
            "--silent",
            "--show-error",
            "--dump-header",
            "-",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
            "--header",
            "x-debug: x-cache",
        ]
        if byte_range is not None:
            arguments.extend(("--range", byte_range))
        if uid is not None:
            arguments.extend(("--header", f"uuid: {uid}"))
        if identity is not None:
            arguments.extend(("--header", f"crr-foo: {identity}"))
        if show_download_size:
            arguments.extend(("--write-out", "SENT: '%{size_download}'"))
        arguments.append(f"http://{host}/{path}")
        return self._curl.run_for(self._ats, *arguments)

    @staticmethod
    def assert_contains(result: CommandResult, *values: str) -> None:
        """Assert all expected response fragments are present."""

        for value in values:
            assert value in result.output, result.output

    def repair_non_reference_block(self) -> None:
        """Replace an old second block and continue the response."""

        self.assert_contains(self.request("crr", "second", byte_range="0-2", uid="etagnew-0"), "bbb", "etagnew")
        self.assert_contains(self.request("crr", "second", byte_range="3-5", uid="etagold-1"), "aa", "etagold")
        healed = self.request("slice", "second", byte_range="3-", uid="etagnew-1")
        assert healed.returncode == 0, healed.output
        self.assert_contains(healed, "bb", "etagnew")
        complete = self.request("slice", "second")
        assert complete.returncode == 0, complete.output
        self.assert_contains(complete, "bbbbb", "etagnew")

    def repair_reference_block(self) -> None:
        """Abort an inconsistent response, heal its reference, and retry."""

        self.assert_contains(self.request("crr", "reference", byte_range="0-2", uid="etagold-0"), "aaa", "etagold")
        self.assert_contains(self.request("crr", "reference", byte_range="3-5", uid="etagnew-1"), "bb", "etagnew")
        aborted = self.request(
            "slice",
            "reference",
            byte_range="3-",
            uid="etagnew-0",
            show_download_size=True,
        )
        self.assert_contains(aborted, "etagold", "SENT: '0'")
        complete = self.request("slice", "reference")
        assert complete.returncode == 0, complete.output
        self.assert_contains(complete, "bbbbb", "etagnew")

    def handle_non_range_and_missing_assets(self) -> None:
        """Pass through a 200 and turn an incomplete cached asset into a 404."""

        full = self.request("slice", "code200", byte_range="3-5", uid="code200")
        assert full.returncode == 0, full.output
        self.assert_contains(full, "200 OK", "ccccc")
        seeded = self.request("slice", "assetgone", byte_range="0-2", uid="assetgone-0")
        assert seeded.returncode == 0, seeded.output
        self.assert_contains(seeded, "aaa", "etag")
        incomplete = self.request("slice", "assetgone")
        self.assert_contains(incomplete, "aaa", "Content-Length: 5", "etag")
        for _attempt in range(20):
            missing = self.request("slice", "assetgone")
            if "404 Not Found" in missing.output:
                break
            time.sleep(0.1)
        else:
            pytest.fail(f"slice did not discard the incomplete cached asset:\n{missing.output}")

    def repair_with_custom_identity(self) -> None:
        """Repair a block when cache_range_requests uses a custom identity header."""

        identity = format_datetime(datetime.now(UTC) + timedelta(seconds=100), usegmt=True)
        old = self.request(
            "crrhdr",
            "second-custom",
            byte_range="3-5",
            uid="etagold-custom-1",
            identity=identity,
        )
        self.assert_contains(old, "aa", "etagold-custom")
        healed = self.request("slicehdr", "second-custom", byte_range="3-", uid="etagnew-custom-1")
        assert healed.returncode == 0, healed.output
        self.assert_contains(healed, "bb", "etagnew-custom")

    def run(self) -> None:
        """Run all repair paths against one persistent cache."""

        self._origin.start()
        self._ats.start()
        self.repair_non_reference_block()
        self.repair_reference_block()
        self.handle_non_range_and_missing_assets()
        self.repair_with_custom_identity()
        wait_for_file_lines(self._ats.diags_log, "logSliceError", 1)


def test_slice_selfhealing(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Slice heals stale blocks, missing assets, and custom identities."""

    SliceSelfHealingScenario(ats_factory, services).run()
