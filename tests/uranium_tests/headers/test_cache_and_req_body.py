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

import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class CacheRequestBodyScenario:
    """Keep cached-response framing safe when GET requests carry bodies."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        if curl.uses_uds:
            pytest.skip("raw netcat requests require a TCP listener")
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create the short-lived cacheable response."""

        origin = services.origin("server")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers":
                    "HTTP/1.1 200 OK\r\n"
                    "Connection: close\r\n"
                    "Last-Modified: Tue, 08 May 2018 15:49:41 GMT\r\n"
                    "Cache-Control: max-age=1\r\n\r\n",
                "body": "xxx",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable cache diagnostics used to distinguish fills and hits."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("xdebug.so"):
            pytest.skip("xdebug.so is not installed")
        ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key,via")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                "proxy.config.http.response_via_str": 3,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        return ats

    def curl_request(self) -> str:
        """Issue the ordinary request used to fill or hit the cache."""

        result = self._curl.run_for(
            self._ats,
            (
                f"--silent --dump-header - --verbose --ipv4 --http1.1 --header 'x-debug: x-cache,x-cache-key,via' "
                f"--header 'Host: www.example.com' 'http://localhost:{self._ats.http_port}/'"),
        )
        assert result.returncode == 0, result.output
        return result.stdout

    def raw_request(self, request: str) -> str:
        """Send one deliberately body-bearing request with netcat."""

        command = (f"printf %s {shlex.quote(request)} | "
                   f"nc 127.0.0.1 -w 1 {self._ats.http_port}")
        result = self._ats.run_shell(command)
        assert result.returncode == 0, result.output
        return result.stdout

    def assert_cached_response(self, response: str, connection: str) -> None:
        """Verify the framing and cache diagnostics of a cached response."""

        lower = response.lower()
        assert "http/1.1 200 ok" in lower
        assert "content-length: 3" in lower
        assert "x-cache: hit-fresh" in lower
        assert f"connection: {connection}" in lower
        assert "xxx" in response

    def run(self) -> None:
        """Fill the cache, then send ordinary and smuggled body-shaped requests."""

        self._origin.start()
        self._ats.start()
        fill = self.curl_request()
        assert "X-Cache: miss" in fill
        assert "X-Cache-Key:" in fill
        self.assert_cached_response(self.curl_request(), "keep-alive")
        hidden_request = (
            "GET / HTTP/1.1\r\n"
            "x-debug: x-cache,x-cache-key,via\r\n"
            "Host: www.example.com\r\n"
            "Content-Length: 71\r\n\r\n"
            "GET /index.html?evil=zorg810 HTTP/1.1\r\n"
            "Host: dummy-host.example.com\r\n\r\n")
        self.assert_cached_response(self.raw_request(hidden_request), "keep-alive")
        truncated_body = (
            "GET / HTTP/1.1\r\n"
            "x-debug: x-cache,x-cache-key,via\r\n"
            "Host: dummy-host.example.com\r\n"
            "Cache-control: max-age=300\r\n"
            "Content-Length: 100\r\n\r\n"
            "GET /index.html?evil=zorg810 HTTP/1.1\r\n"
            "Host: dummy-host.example.com\r\n\r\n")
        self.assert_cached_response(self.raw_request(truncated_body), "close")


def test_cache_and_req_body(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Cached responses remain correctly framed around body-bearing GET requests."""

    CacheRequestBodyScenario(ats_factory, services, curl).run()
