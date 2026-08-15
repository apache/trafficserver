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
import shutil
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


class ServerPushPreloadScenario:
    """Translate eligible Link preload headers into HTTP/2 server pushes."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Serve one document, one pushed script, and one nopush stylesheet."""

        origin = services.origin("origin")
        html = (
            "<html>\r\n<head>\r\n<link rel='stylesheet' type='text/css' href='/app/style.css' />\r\n"
            "<script src='/app/script.js'></script>\r\n</head>\r\n<body>\r\nServer Push Preload Test\r\n"
            "</body>\r\n</html>\r\n")
        origin.add_response(
            {
                "headers": "GET /index.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers":
                    (
                        f"HTTP/1.1 200 OK\r\nContent-Length: {len(html)}\r\nConnection: close\r\n"
                        "Link: </app/style.css>; rel=preload; as=style; nopush\r\n"
                        "Link: </app/script.js>; rel=preload; as=script\r\n\r\n"),
                "body": html,
            },
        )
        for path, body in (
            ("/app/style.css", "body { font-weight: bold; }\r\n"),
            ("/app/script.js", "function do_nothing() { return; }\r\n"),
        ):
            origin.add_response(
                {
                    "headers": f"GET {path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": f"HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n",
                    "body": body,
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable TLS and the server_push_preload remap plugin."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        if not ats.plugin_exists("server_push_preload.so"):
            pytest.skip("server_push_preload.so is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http2|server_push_preload",
                "proxy.config.http2.active_timeout_in": 3,
            })
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/ @plugin=server_push_preload.so")
        return ats

    def run_client(self) -> str:
        """Use nghttp because Proxy Verifier does not expose pushed streams."""

        if shutil.which("nghttp") is None:
            pytest.skip("nghttp is required")
        result = subprocess.run(
            ("nghttp", "-vs", "--no-dep", f"https://127.0.0.1:{self._ats.https_port}/index.html"),
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

    def run(self) -> None:
        """Run the HTTP/2 client and compare the pushed stream trace."""

        self._origin.start()
        self._ats.start()
        assert_matches_gold(self.run_client(), TEST_DIRECTORY / "gold/server_push_preload_0_stdout.gold")


def test_server_push_preload(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Only the Link entry without nopush creates a pushed stream."""

    ServerPushPreloadScenario(ats_factory, services).run()
