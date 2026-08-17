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
"""Verify caching complete responses to normalized and raw range requests."""

import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class CacheCompleteResponsesScenario:
    """Exercise complete-response range caching through three ordered rounds."""

    SMALL_BODY = "x" * 10_000
    SLICE_BODY_LENGTH = 4 * 1024 * 1024
    SLICE_BODY = "x" * SLICE_BODY_LENGTH

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @staticmethod
    def add_response(
        origin: OriginServer,
        uid: str,
        *,
        status: str,
        cache_control: str,
        etag: str,
        body: str = "",
        content_range: str | None = None,
    ) -> None:
        """Add one UID-selected origin response."""

        fields = [f"HTTP/1.1 {status}", f"Cache-Control: {cache_control}", "Connection: close", f"Etag: {etag}"]
        if content_range is not None:
            fields.append(f"Content-Range: bytes {content_range}")
        origin.add_response(
            {"headers": f"GET {{PATH}} HTTP/1.1\r\nHost: www.example.com\r\nUID: {uid}\r\n\r\n"},
            {
                "headers": "\r\n".join(fields) + "\r\n\r\n",
                "body": body
            },
        )

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create small, sliced, and conditional origin responses."""

        origin = services.origin("origin", lookup_key="{%UID}")
        cls.add_response(
            origin,
            "SMALL",
            status="200 OK",
            cache_control="max-age=1",
            etag='"772102f4-56f4bc1e6d417"',
            body=cls.SMALL_BODY,
        )
        cls.add_response(
            origin,
            "SMALL-INM",
            status="304 Not Modified",
            cache_control="max-age=10",
            etag='"772102f4-56f4bc1e6d417"',
        )
        cls.add_response(
            origin,
            "SLICE",
            status="206 Partial Content",
            cache_control="max-age=1",
            etag='"872104f4-d6bcaa1e6f979"',
            content_range=f"0-{cls.SLICE_BODY_LENGTH - 1}/{cls.SLICE_BODY_LENGTH * 2}",
            body=cls.SLICE_BODY,
        )
        cls.add_response(
            origin,
            "SLICE-INM",
            status="304 Not Modified",
            cache_control="max-age=10",
            etag='"872104f4-d6bcaa1e6f979"',
        )
        cls.add_response(
            origin,
            "NAIEVE",
            status="200 OK",
            cache_control="max-age=1",
            etag='"cad04ff4-56f4bc197ceda"',
            body=cls.SMALL_BODY,
        )
        cls.add_response(
            origin,
            "NAIEVE-INM",
            status="304 Not Modified",
            cache_control="max-age=10",
            etag='"cad04ff4-56f4bc197ceda"',
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure normalized and raw complete-response range mappings."""

        ats = ats_factory.create("ats")
        required = ("cachekey.so", "cache_range_requests.so", "slice.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("cachekey.so, cache_range_requests.so, slice.so, and xdebug.so are required")
        origin = f"http://127.0.0.1:{self._origin.port}"
        ats.remap_config.add_lines(
            (
                f"map http://example.com/naieve {origin}/naieve @plugin=cache_range_requests.so "
                "@pparam=--cache-complete-responses",
                f"map http://example.com {origin} @plugin=slice.so @pparam=--blockbytes=4m "
                "@plugin=cachekey.so @pparam=--key-type=cache_key @pparam=--include-headers=Range "
                "@pparam=--remove-all-params=true @plugin=cache_range_requests.so "
                "@pparam=--no-modify-cachekey @pparam=--cache-complete-responses",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "cachekey|cache_range_requests|slice",
            })
        return ats

    def request(self, path: str, byte_range: str, uid: str) -> CommandResult:
        """Issue one debug-enabled range request."""

        return self._curl.run_for(
            self._ats,
            (
                f"--silent --show-error --dump-header - --proxy 'http://127.0.0.1:{self._ats.http_port}' --header "
                f"'x-debug: x-cache, x-cache-key' --header 'UID: {uid}' --range '{byte_range}' "
                f"'http://example.com{path}'"),
        )

    @staticmethod
    def assert_response(
        result: CommandResult,
        *,
        status: str,
        cache: str,
        cache_key: str,
        content_range: str | None = None,
    ) -> None:
        """Verify status, cache state, cache key, and optional range."""

        assert result.returncode == 0, result.output
        assert status in result.stdout, result.output
        assert f"X-Cache: {cache}" in result.stdout, result.output
        assert re.search(cache_key, result.stdout), result.output
        if content_range is None:
            assert "Content-Range:" not in result.stdout, result.output
        else:
            assert f"Content-Range: bytes {content_range}" in result.stdout, result.output

    def run_small_object_round(self) -> None:
        """Verify normalized ranges share a complete 200 response."""

        key = r"X-Cache-Key: /.*?/Range:bytes=0-4194303/obj"
        self.assert_response(self.request("/obj", "0-5000", "SMALL"), status="200 OK", cache="miss, none", cache_key=key)
        self.assert_response(self.request("/obj", "5001-5999", "SMALL"), status="200 OK", cache="hit-fresh, none", cache_key=key)
        time.sleep(2)
        self.assert_response(self.request("/obj", "0-403", "SMALL-INM"), status="200 OK", cache="hit-stale, none", cache_key=key)
        self.assert_response(self.request("/obj", "0-3999", "SMALL"), status="200 OK", cache="hit-fresh, none", cache_key=key)

    def run_sliced_object_round(self) -> None:
        """Verify normalized ranges share a cached four-megabyte slice."""

        key = r"X-Cache-Key: /.*?/Range:bytes=0-4194303/slice"
        self.assert_response(
            self.request("/slice", "0-5000", "SLICE"),
            status="206 Partial Content",
            cache="miss, none",
            cache_key=key,
            content_range="0-5000/8388608",
        )
        self.assert_response(
            self.request("/slice", "5001-5999", "SLICE"),
            status="206 Partial Content",
            cache="hit-fresh, none",
            cache_key=key,
            content_range="5001-5999/8388608",
        )
        time.sleep(2)
        self.assert_response(
            self.request("/slice", "0-403", "SLICE-INM"),
            status="206 Partial Content",
            cache="hit-stale, none",
            cache_key=key,
            content_range="0-403/8388608",
        )
        self.assert_response(
            self.request("/slice", "0-3999", "SLICE"),
            status="206 Partial Content",
            cache="hit-fresh, none",
            cache_key=key,
            content_range="0-3999/8388608",
        )

    def run_raw_range_round(self) -> None:
        """Show that unnormalized Range values create separate cache objects."""

        original_key = r"X-Cache-Key: http://.*?/naieve/obj-bytes=0-5000"
        alternate_key = r"X-Cache-Key: http://.*?/naieve/obj-bytes=444-777"
        self.assert_response(self.request("/naieve/obj", "0-5000", "NAIEVE"), status="200 OK", cache="miss", cache_key=original_key)
        self.assert_response(
            self.request("/naieve/obj", "0-5000", "NAIEVE"),
            status="200 OK",
            cache="hit-fresh",
            cache_key=original_key,
        )
        time.sleep(2)
        self.assert_response(
            self.request("/naieve/obj", "0-5000", "NAIEVE-INM"),
            status="200 OK",
            cache="hit-stale",
            cache_key=original_key,
        )
        self.assert_response(
            self.request("/naieve/obj", "0-5000", "NAIEVE"),
            status="200 OK",
            cache="hit-fresh",
            cache_key=original_key,
        )
        self.assert_response(
            self.request("/naieve/obj", "444-777", "NAIEVE"), status="200 OK", cache="miss", cache_key=alternate_key)
        self.assert_response(
            self.request("/naieve/obj", "444-777", "NAIEVE"), status="200 OK", cache="hit", cache_key=alternate_key)
        self.assert_response(
            self.request("/naieve/obj", "0-5000", "NAIEVE"),
            status="200 OK",
            cache="hit-fresh",
            cache_key=original_key,
        )

    def run(self) -> None:
        """Run all rounds against one persistent cache."""

        self._origin.start()
        self._ats.start()
        self.run_small_object_round()
        self.run_sliced_object_round()
        self.run_raw_range_round()


def test_cache_range_requests_cache_complete_responses(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Complete responses remain cacheable while normalized ranges share keys."""

    CacheCompleteResponsesScenario(ats_factory, services).run()
