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

from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, assert_matches_gold, send_tcp

TEST_DIRECTORY = Path(__file__).parent


class ComboHandlerScenario:
    """Fetch and combine origin assets through combo_handler."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)
        if not self._ats.plugin_exists("combo_handler.so"):
            pytest.skip("combo_handler.so is not installed")

    @staticmethod
    def add_object(
        origin: OriginServer,
        content_type: str,
        path: str,
        cache_control: str = "public, max-age=31536000",
    ) -> None:
        """Add one cacheable object to the microserver."""

        origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: just.any.thing\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nConnection: close\r\nEtag: \"359670651\"\r\n"
                        f"Cache-Control: {cache_control}\r\nAccept-Ranges: bytes\r\nContent-Type: {content_type}\r\n\r\n"),
                "body": f"Content for {path}\n",
            },
        )

    @classmethod
    def configure_origin(cls, services: ServiceFactory) -> OriginServer:
        """Create every asset used by the combo requests."""

        origin = services.origin("origin")
        cls.add_object(origin, "text/css ; charset=utf-8", "/obj1")
        cls.add_object(origin, "text/javascript", "/sub/obj2")
        cls.add_object(origin, "text/argh", "/obj3")
        cls.add_object(origin, "application/javascript", "/obj4")
        cls.add_object(origin, "application/javascript", "/s/assets/module:variant_v1.js")
        cls.add_object(origin, "", "/obj_empty_ct")
        cls.add_object(origin, "text/javascript", "/obj_priv_short", "private, max-age=60")
        cls.add_object(origin, "text/javascript", "/obj_revalidate", "public, max-age=0")
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the global and remap combo_handler instances."""

        ats = ats_factory.create("ts", disable_log_checks=True)
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|combo_handler",
        })
        ats.plugin_config.add_line("combo_handler.so - - - ctwl.txt")
        ats.remap_config.add_lines(
            (
                "map http://xyz/ http://127.0.0.1/ @plugin=combo_handler.so",
                f"map http://localhost/127.0.0.1/ http://127.0.0.1:{self._origin.http_port}/",
                f"map http://localhost/sub/ http://127.0.0.1:{self._origin.http_port}/sub/",
                f"map http://localhost/s/ http://127.0.0.1:{self._origin.http_port}/s/",
            ))
        ats.copy_to_config(TEST_DIRECTORY / "ctwl.txt")
        return ats

    def request(self, query: str) -> str:
        """Send one raw combo request and return its chunked response."""

        return send_tcp(
            self._ats.http_port,
            f"GET /admin/v1/combo?{query} HTTP/1.1\nHost: xyz\nConnection: close\n\n",
        )

    def run(self) -> None:
        """Verify type filtering, colon paths, and cache-control merging."""

        self._origin.start()
        self._ats.start()
        gold = TEST_DIRECTORY / "combo_handler_files"
        cases = (
            ("obj1&sub:obj2&obj3", "tr1.gold"),
            ("obj1&sub:obj2&obj4", "tr2.gold"),
            ("obj1&obj_empty_ct", "tr3.gold"),
            ("obj1&obj_priv_short", "cache_control_aggregation.gold"),
            ("obj1&obj_revalidate", "max_age_zero.gold"),
        )
        for query, filename in cases:
            assert_matches_gold(self.request(query), gold / filename)

        colon_response = self.request("s:assets/module:variant_v1.js")
        assert "HTTP/1.1 200 OK" in colon_response
        assert "Content for /s/assets/module:variant_v1.js" in colon_response
        assert "ERROR" in self._ats.diags_log.read_text(errors="replace")


def test_combo_handler(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """combo_handler combines only allowed objects and merges cache controls."""

    ComboHandlerScenario(ats_factory, services).run()
