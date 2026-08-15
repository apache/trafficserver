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

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory


class StaleRedirectScenario:
    """Refresh a stale cached object whose origin response redirects."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Redirect `/obj` to a short-lived cacheable `/obj2` response."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET /obj HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers": f"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{origin.port}/obj2\r\n\r\n",
                "body": "",
            },
        )
        origin.add_response(
            {
                "headers": "GET /obj2 HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers": ("HTTP/1.1 200 OK\r\nX-Obj: obj2\r\nCache-Control: max-age=2\r\n"
                            "Content-Length: 0\r\n\r\n"),
                "body": "",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable redirect following and caching without required headers."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|cache|redirect",
                "proxy.config.http.cache.required_headers": 0,
                "proxy.config.http.push_method_enabled": 1,
                "proxy.config.url_remap.remap_required": 0,
                "proxy.config.http.redirect.actions": "routable:follow,loopback:follow,self:follow",
                "proxy.config.http.number_of_redirections": 1,
            })
        return ats

    def request(self) -> CommandResult:
        """Fetch the cached URL with the origin port in its Host header."""

        result = self._curl.run_for(
            self._ats,
            "--silent",
            "--dump-header",
            "/dev/stdout",
            "--header",
            f"Host: 127.0.0.1:{self._origin.port}",
            f"http://127.0.0.1:{self._ats.http_port}/obj",
        )
        assert result.returncode == 0, result.output
        return result

    @staticmethod
    def verify_response(result: CommandResult) -> None:
        """Require the final redirected representation."""

        assert "200 OK" in result.stdout
        assert "X-Obj: obj2".lower() in result.stdout.lower()

    def run(self) -> None:
        """Populate the cache, let it expire, and refresh through the redirect."""

        self._origin.start()
        self._ats.start()
        self.verify_response(self.request())
        time.sleep(4)
        self.verify_response(self.request())


def test_redirect_stale(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Redirect following remains active during stale cache refresh."""

    StaleRedirectScenario(ats_factory, services, curl).run()
