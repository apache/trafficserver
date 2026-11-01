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


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Redirect `/obj` to a short-lived cacheable `/obj2` response.

    :param services: Factory owning support services and their cleanup.
    """

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


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Enable redirect following and caching without required headers.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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


def request(*, _ats: ATS, _curl: Curl, _origin: OriginServer) -> CommandResult:
    """Fetch the cached URL with the origin port in its Host header.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    result = _curl.run_for(
        _ats,
        (
            f"--silent --dump-header /dev/stdout --header 'Host: 127.0.0.1:{_origin.port}' "
            f"'http://127.0.0.1:{_ats.http_port}/obj'"),
    )
    assert result.returncode == 0, result.output
    return result


def verify_response(result: CommandResult) -> None:
    """Require the final redirected representation.

    :param result: Completed command result to validate.
    """

    assert "200 OK" in result.stdout
    assert "X-Obj: obj2".lower() in result.stdout.lower()


def test_redirect_stale(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Redirect following remains active during stale cache refresh.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory)

    _origin.start()
    _ats.start()
    verify_response(request(_ats=_ats, _curl=curl, _origin=_origin))
    time.sleep(4)
    verify_response(request(_ats=_ats, _curl=curl, _origin=_origin))
