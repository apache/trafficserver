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

import time

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class CacheFillScenario:
    """Exercise cache_fill as both a remap and global plugin."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("cache_fill does not support the UDS test transport")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def _response(cache_control: str, etag: str) -> dict[str, str]:
        return {
            "headers": (f"HTTP/1.1 200 OK\r\nCache-Control: {cache_control}\r\nConnection: close\r\n"
                        f"Etag: {etag}\r\n\r\n"),
            "body": "hello hello",
        }

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create cacheable, range, no-store, and global responses."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /200 HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            self._response("max-age=1", "772102f4-56f4bc1e6d417"),
        )
        for path, cache_control, etag in (
            ("range", "max-age=1", "883213f5-67f5bc2e7d528"),
            ("nostore", "nostore", "994324f6-78f6bc3e8d639"),
        ):
            origin.add_response(
                {
                    "headers": (f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\n"
                                "Accept: */*\r\nRange: bytes=0-4\r\n\r\n"),
                    "body": "",
                },
                self._response(cache_control, etag),
            )
        origin.add_response(
            {
                "headers": "GET /global HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            self._response("max-age=1", "661091f3-45f3bc0e5d306"),
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Load xdebug and install cache_fill globally and on remap rules."""

        ats = ats_factory.create("ts")
        missing = [plugin for plugin in ("cache_fill.so", "xdebug.so") if not ats.plugin_exists(plugin)]
        if missing:
            pytest.skip(f"Required plugins are unavailable: {', '.join(missing)}")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "cache_fill|.*cache.*",
        })
        ats.plugin_config.add_lines(("xdebug.so --enable=x-cache,x-cache-key", "cache_fill.so"))
        for path, origin_path in (("200", "200"), ("range", "range"), ("nostore", "nostore"), ("304", "range")):
            ats.remap_config.add_line(
                f"map http://www.example.com/{path} http://127.0.0.1:{self._origin.port}/{origin_path} "
                "@plugin=cache_fill.so")
        ats.remap_config.add_line(f"map http://www.example.com/global http://127.0.0.1:{self._origin.port}/global")
        return ats

    def request(self, path: str, *, byte_range: bool = False) -> CommandResult:
        """Fetch one resource with xdebug cache headers enabled."""

        arguments = [
            "--silent",
            "--dump-header",
            "/dev/stdout",
            "--verbose",
            "--proxy",
            f"localhost:{self._ats.http_port}",
            "--header",
            "x-debug: x-cache,x-cache-key",
        ]
        if byte_range:
            arguments.extend(("--range", "0-4"))
        arguments.append(f"http://www.example.com/{path}")
        result = self._curl.run_for(self._ats, *arguments)
        assert result.returncode == 0, result.output
        return result

    @staticmethod
    def require_response(result: CommandResult, cache_status: str, http_status: str) -> None:
        """Require an HTTP status and xdebug cache classification."""

        assert f"X-Cache: {cache_status}".lower() in result.stdout.lower(), result.output
        assert http_status in result.stdout, result.output

    def run(self) -> None:
        """Verify background fill, range synthesis, no-store, and global mode."""

        self._origin.start()
        self._ats.start()
        self.require_response(self.request("200"), "miss", "200 OK")
        self.require_response(self.request("200"), "hit-fresh", "200 OK")
        self.require_response(self.request("range", byte_range=True), "miss", "200 OK")
        ranged = self.request("range", byte_range=True)
        self.require_response(ranged, "hit-fresh", "206 Partial Content")
        assert "Content-Range: bytes 0-4/11".lower() in ranged.stdout.lower()
        self.require_response(self.request("nostore", byte_range=True), "miss", "200 OK")
        self.require_response(self.request("nostore", byte_range=True), "miss", "200 OK")
        self.require_response(self.request("global"), "miss", "200 OK")
        time.sleep(0.1)
        self.require_response(self.request("global"), "hit-fresh", "200 OK")


def test_cache_fill(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """cache_fill populates eligible objects and leaves no-store objects alone."""

    CacheFillScenario(ats_factory, services, curl).run()
