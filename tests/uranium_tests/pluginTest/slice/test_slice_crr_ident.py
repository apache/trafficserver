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
"""Verify slice and cache_range_requests identity coordination."""

import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines


class SliceCrrIdentScenario:
    """Exercise stale slices whose validators identify one logical asset."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._origin = self.configure_server()
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    def add_asset(self, uid: str, etag: str, max_age: int, bodies: tuple[str, str]) -> None:
        """Add the two three-byte slice responses for an asset generation."""

        for index, (byte_range, content_range, body) in enumerate((("0-2", "0-2/5", bodies[0]), ("3-5", "3-4/5", bodies[1]))):
            self._origin.add_response(
                {
                    "headers":
                        (
                            f"GET /plain HTTP/1.1\r\nHost: www.example.com\r\n"
                            f"UID: {uid} {index}\r\nRange: bytes={byte_range}\r\n\r\n")
                },
                {
                    "headers":
                        (
                            "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n"
                            f"Cache-Control: max-age={max_age}\r\nConnection: close\r\n"
                            f'Content-Range: bytes {content_range}\r\nEtag: "{etag}"\r\n\r\n'),
                    "body": body,
                },
            )

    def configure_server(self) -> OriginServer:
        """Create UID-keyed old and replacement slice generations."""

        origin = self._services.origin("origin", lookup_key="{%UID}")
        self._origin = origin
        self.add_asset("plain", "plain", 1, ("aaa", "BB"))
        self.add_asset("chg", "chg", 60, ("AAA", "bb"))
        origin.add_response(
            {"headers": "GET /404.txt HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n",
                "body": "Not Found"
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Chain slice into cache_range_requests and add transaction logging."""

        ats = ats_factory.create("ats")
        required = ("cache_range_requests.so", "header_rewrite.so", "slice.so", "xdebug.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("slice, cache_range_requests, header_rewrite, and xdebug are required")
        ats.write_config_file(
            "hdr_rw.conf",
            "\n".join(
                (
                    "cond %{SEND_REQUEST_HDR_HOOK}",
                    'cond %{HEADER:Range} ="bytes=0-2" [AND]',
                    "set-header UID %{CLIENT-HEADER:UID} 0",
                    "cond %{SEND_REQUEST_HDR_HOOK}",
                    'cond %{HEADER:Range} ="bytes=3-5" [AND]',
                    "set-header UID %{CLIENT-HEADER:UID} 1",
                )) + "\n",
        )
        ats.remap_config.add_lines(
            (
                f"map http://slice/ http://127.0.0.1:{self._origin.port}/ "
                "@plugin=slice.so @pparam=--blockbytes-test=3 @pparam=--remap-host=crr",
                f"map http://crr/ http://127.0.0.1:{self._origin.port}/ "
                "@plugin=cache_range_requests.so @pparam=--consider-ims @pparam=--consider-ident "
                "@plugin=header_rewrite.so @pparam=hdr_rw.conf",
            ))
        ats.plugin_config.add_line("xdebug.so --enable=x-cache")
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
                                            "cpuup=%<cquup> sssc=%<sssc> pssc=%<pssc> phr=%<phr> "
                                            "range=::%<{Range}cqh>:: x-crr-ident=::%<{X-Crr-Ident}cqh>:: "
                                            "uid=::%<{UID}pqh>:: crc=%<crc>"),
                                }
                            ],
                        "logs": [{
                            "filename": "transaction",
                            "format": "custom"
                        }],
                    }
            })
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "cache_range_requests|header_rewrite|slice|log",
                "proxy.config.log.max_secs_per_buffer": 1,
            })
        return ats

    def request(self, host: str, path: str, *, uid: str | None = None) -> str:
        """Request an object and return headers plus body."""

        headers = {"x-debug": "x-cache"}
        if uid is not None:
            headers["UID"] = uid
        arguments = ["--silent", "--dump-header", "-", "--proxy", f"http://127.0.0.1:{self._ats.http_port}"]
        for name, value in headers.items():
            arguments.extend(("--header", f"{name}: {value}"))
        arguments.append(f"http://{host}{path}")
        result = self._curl.run_for(self._ats, *arguments)
        assert result.returncode == 0, result.output
        return result.stdout

    def run(self) -> None:
        """Replace stale slices, verify hits, and compare the transaction log."""

        self._origin.start()
        self._ats.start()
        output = self.request("slice", "/plain", uid="plain")
        assert "aaaBB" in output and 'Etag: "plain"' in output
        time.sleep(2)
        output = self.request("slice", "/plain", uid="plain")
        assert "aaaBB" in output and 'Etag: "plain"' in output
        time.sleep(2)
        output = self.request("slice", "/plain", uid="chg")
        assert "AAAbb" in output and 'Etag: "chg"' in output
        time.sleep(2)
        output = self.request("slice", "/plain", uid="chg")
        assert "AAAbb" in output and 'Etag: "chg"' in output
        assert "404" in self.request("crr", "/404.txt")
        transaction_log = self._ats.log_directory / "transaction.log"
        content = wait_for_file_lines(transaction_log, "404.txt", 1, timeout=15)
        assert_matches_gold(content, self._services.resolve_path("gold/slice_crr_ident.gold"))


def test_slice_crr_ident(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Slice validation uses cache_range_requests identity metadata."""

    SliceCrrIdentScenario(ats_factory, services).run()
