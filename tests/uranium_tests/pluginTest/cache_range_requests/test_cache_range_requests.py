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
"""Verify cache_range_requests keys, statuses, and long-key spill handling."""

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class CacheRangeRequestsScenario:
    """Exercise range caching and parent-selection key options."""

    BODY = "lets go surfin now"
    LONG_PATH = "A" * 16400

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @classmethod
    def add_response(
        cls,
        origin: OriginServer,
        uuid: str | None,
        *,
        status: str,
        body: str,
        content_range: str | None = None,
        etag: str = '"path"',
    ) -> None:
        """Add a UUID-keyed cacheable origin response."""

        uuid_line = "" if uuid is None else f"uuid: {uuid}\r\n"
        fields = [f"HTTP/1.1 {status}", "Connection: close"]
        if status.startswith(("200", "206")):
            fields.extend(("Cache-Control: max-age=500", f"Etag: {etag}"))
        if content_range is not None:
            fields.extend(("Accept-Ranges: bytes", f"Content-Range: bytes {content_range}"))
        origin.add_response(
            {"headers": f"GET /path HTTP/1.1\r\nHost: www.example.com\r\n{uuid_line}\r\n"},
            {
                "headers": "\r\n".join(fields) + "\r\n\r\n",
                "body": body
            },
        )

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create full, ranged, parent-keyed, long-key, and 404 responses."""

        origin = services.origin("origin", lookup_key="{%uuid}")
        cls.add_response(origin, "full", status="200 OK", body=cls.BODY)
        cls.add_response(origin, "inner", status="206 Partial Content", body=cls.BODY[7:15], content_range="7-15/18")
        cls.add_response(origin, "frange", status="206 Partial Content", body=cls.BODY, content_range="0-18/18")
        cls.add_response(origin, "last", status="206 Partial Content", body=cls.BODY[-5:], content_range="13-18/18")
        cls.add_response(origin, "pselect", status="206 Partial Content", body=cls.BODY[1:10], content_range="1-10/19")
        cls.add_response(
            origin,
            "long_key",
            status="206 Partial Content",
            body=cls.BODY,
            content_range="0-17/18",
            etag='"longkey"',
        )
        cls.add_response(origin, None, status="404 Not Found", body="Not Found")
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure standard, parent-selection, deprecated, and long-key mappings."""

        ats = ats_factory.create("ats")
        required = ("cache_range_requests.so", "header_rewrite.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("cache_range_requests.so, header_rewrite.so, and xdebug.so are required")
        ats.copy_to_config("reason.conf")
        origin = f"http://127.0.0.1:{self._origin.port}"
        ats.remap_config.add_lines(
            (
                f"map http://www.example.com {origin} @plugin=header_rewrite.so "
                f"@pparam={ats.config_directory}/reason.conf @plugin=cache_range_requests.so",
                f"map http://www.longkey.com {origin} @plugin=cache_range_requests.so",
                f"map http://parentselect {origin} @plugin=cache_range_requests.so @pparam=--ps-cachekey",
                f"map http://psd {origin} @plugin=cache_range_requests.so @pparam=ps_mode:cache_key_url",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-parentselection-key")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "cache_range_requests|http",
        })
        return ats

    def request(
        self,
        host: str,
        path: str,
        *,
        byte_range: str | None = None,
        uuid: str | None = None,
        debug: str = "x-cache",
    ) -> CommandResult:
        """Issue a proxied range request and return its complete result."""

        arguments = [
            "--silent",
            "--show-error",
            "--dump-header",
            "-",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
            "--header",
            f"x-debug: {debug}",
        ]
        if byte_range is not None:
            arguments.extend(("--range", byte_range))
        if uuid is not None:
            arguments.extend(("--header", f"uuid: {uuid}"))
        arguments.append(f"http://{host}{path}")
        return self._curl.run_for(self._ats, *arguments)

    @staticmethod
    def assert_range(result: CommandResult, *, cache: str, content_range: str, body: str) -> None:
        """Verify one successful plugin range response."""

        assert result.returncode == 0, result.output
        assert "206 Foo Bar" in result.stdout, result.output
        assert f"X-Cache: {cache}" in result.stdout, result.output
        assert f"Content-Range: bytes {content_range}" in result.stdout, result.output
        assert body in result.stdout, result.output

    def run(self) -> None:
        """Exercise misses, hits, errors, parent keys, and the spill path."""

        self._origin.start()
        self._ats.start()
        full = self.request("www.example.com", "/path", uuid="full")
        assert full.returncode == 0 and self.BODY in full.stdout
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="7-15", uuid="inner"),
            cache="miss",
            content_range="7-15/18",
            body=self.BODY[7:15],
        )
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="7-15"),
            cache="hit",
            content_range="7-15/18",
            body=self.BODY[7:15],
        )
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="0-", uuid="frange"),
            cache="miss",
            content_range="0-18/18",
            body=self.BODY,
        )
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="0-"),
            cache="hit",
            content_range="0-18/18",
            body=self.BODY,
        )
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="-5", uuid="last"),
            cache="miss",
            content_range="13-18/18",
            body=self.BODY[-5:],
        )
        self.assert_range(
            self.request("www.example.com", "/path", byte_range="-5"),
            cache="hit",
            content_range="13-18/18",
            body=self.BODY[-5:],
        )
        for _index in range(2):
            missing = self.request("www.example.com", "/404", byte_range="0-")
            assert "404 Not Found" in missing.stdout and "X-Cache: miss" in missing.stdout
        full_range = self.request("www.example.com", "/path?origin-200", byte_range="7-15", uuid="full")
        assert "200 OK" in full_range.stdout and "X-Cache: miss" in full_range.stdout
        assert "Content-Range:" not in full_range.stdout
        parent = self.request("parentselect", "/path", byte_range="1-10", uuid="pselect", debug="x-parentselection-key")
        assert re.search(r"X-ParentSelection-Key: .*-bytes=", parent.stdout), parent.output
        ordinary = self.request("www.example.com", "/path", byte_range="7-15", uuid="inner", debug="x-parentselection-key")
        assert "X-ParentSelection-Key" not in ordinary.stdout
        deprecated = self.request("psd", "/path", byte_range="1-10", uuid="pselect", debug="x-parentselection-key")
        assert re.search(r"X-ParentSelection-Key: .*-bytes=", deprecated.stdout), deprecated.output
        long_key = self.request("www.longkey.com", f"/{self.LONG_PATH}", byte_range="0-17", uuid="long_key")
        assert long_key.returncode == 0 and "206" in long_key.stdout
        assert "disabling cache for this transaction" not in self._ats.diags_log.read_text(errors="replace")


def test_cache_range_requests(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Range cache keys remain correct for hits, parent selection, and long URLs."""

    CacheRangeRequestsScenario(ats_factory, services).run()
