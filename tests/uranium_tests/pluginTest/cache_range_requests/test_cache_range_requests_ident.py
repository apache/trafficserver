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
"""Verify cache_range_requests identity headers control freshness."""

import time
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class CacheRangeIdentScenario:
    """Exercise ETag, Last-Modified, forced-stale, and custom identity headers."""

    BODY = "lets go surfin now"
    ETAG = '"772102f4-56f4bc1e6d417"'
    LAST_MODIFIED = "Fri, 07 Mar 2025 18:06:58 GMT"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @classmethod
    def add_asset(
        cls,
        origin: OriginServer,
        path: str,
        *,
        etag: str | None,
        last_modified: str | None,
        max_age: int,
    ) -> None:
        """Add one cacheable full-range response."""

        fields = [
            "HTTP/1.1 206 Partial Content",
            "Accept-Ranges: bytes",
            f"Cache-Control: max-age={max_age}",
            f"Content-Range: bytes 0-{len(cls.BODY)}/{len(cls.BODY)}",
            "Connection: close",
        ]
        if etag is not None:
            fields.append(f"Etag: {etag}")
        if last_modified is not None:
            fields.append(f"Last-Modified: {last_modified}")
        origin.add_response(
            {"headers": (f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\nAccept: */*\r\nRange: bytes=0-\r\n\r\n")},
            {
                "headers": "\r\n".join(fields) + "\r\n\r\n",
                "body": cls.BODY
            },
        )

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> OriginServer:
        """Create short- and long-lived identity combinations."""

        origin = services.origin("origin")
        cls.add_asset(origin, "both", etag=cls.ETAG, last_modified=cls.LAST_MODIFIED, max_age=1)
        cls.add_asset(origin, "etag", etag=cls.ETAG, last_modified=None, max_age=1)
        cls.add_asset(origin, "lm", etag=None, last_modified=cls.LAST_MODIFIED, max_age=1)
        cls.add_asset(origin, "custom", etag="foo", last_modified=None, max_age=1)
        cls.add_asset(origin, "fresh", etag="fresh", last_modified=cls.LAST_MODIFIED, max_age=3600)
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure standard and custom identity-header mappings."""

        ats = ats_factory.create("ats")
        if not ats.plugin_exists("cache_range_requests.so") or not ats.plugin_exists("xdebug.so"):
            pytest.skip("cache_range_requests.so and xdebug.so are required")
        ats.remap_config.add_lines(
            (
                f"map http://ident http://127.0.0.1:{self._origin.port} "
                "@plugin=cache_range_requests.so @pparam=--consider-ident",
                f"map http://identheader http://127.0.0.1:{self._origin.port} "
                "@plugin=cache_range_requests.so @pparam=--consider-ident @pparam=--ident-header=CrrIdent",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "cache_range_requests",
        })
        return ats

    def request(self, host: str, path: str, expected_cache: str, ident: str | None = None) -> None:
        """Issue a full-range request and verify its x-cache state."""

        arguments = [
            "--silent",
            "--dump-header",
            "-",
            "--output",
            "/dev/null",
            "--proxy",
            f"http://127.0.0.1:{self._ats.http_port}",
            "--header",
            "x-debug: x-cache",
            "--range",
            "0-",
        ]
        if ident is not None:
            header = "CrrIdent" if host == "identheader" else "X-Crr-Ident"
            arguments.extend(("--header", f"{header}: {ident}"))
        arguments.append(f"http://{host}/{path}")
        result = self._curl.run_for(
            self._ats,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output
        assert f"X-Cache: {expected_cache}" in result.stdout, result.output

    def run(self) -> None:
        """Drive stale-to-fresh and fresh-to-stale identity transitions."""

        self._origin.start()
        self._ats.start()
        for path in ("both", "etag", "lm"):
            self.request("ident", path, "miss")
        time.sleep(2)
        self.request("ident", "both", "hit-fresh", f"Etag {self.ETAG}")
        self.request("ident", "both", "hit-stale", f"Last-Modified {self.LAST_MODIFIED}")
        self.request("ident", "both", "hit-stale", "Etag no_match")
        self.request("ident", "etag", "hit-fresh", f"Etag {self.ETAG}")
        self.request("ident", "etag", "hit-stale", f"Last-Modified {self.LAST_MODIFIED}")
        self.request("ident", "etag", "hit-stale", "Etag no_match")
        self.request("ident", "lm", "hit-fresh", f"Last-Modified {self.LAST_MODIFIED}")
        self.request("ident", "lm", "hit-stale", f"Etag {self.ETAG}")
        self.request("ident", "fresh", "miss")
        self.request("ident", "fresh", "hit-fresh")
        self.request("ident", "fresh", "hit-stale", "Etag not_the_same")
        self.request("ident", "fresh", "hit-stale", f"Last-Modified {self.LAST_MODIFIED}")
        self.request("ident", "fresh", "hit-fresh", "Etag fresh")
        self.request("ident", "fresh", "hit-fresh")
        self.request("ident", "fresh", "hit-stale", "Stale")
        self.request("identheader", "custom", "miss")
        self.request("identheader", "custom", "hit-fresh", "Etag foo")


def test_cache_range_requests_ident(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Identity hints override ordinary cached-object freshness as configured."""

    CacheRangeIdentScenario(ats_factory, services).run()
