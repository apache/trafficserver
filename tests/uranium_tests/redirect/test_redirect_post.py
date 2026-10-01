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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

REDIRECT_POST__body_size = 50 * 1024 * 1024


def configure_origins(services: ServiceFactory) -> tuple[OriginServer, OriginServer, OriginServer]:
    """Create the two redirect hops and final 204 response.

    :param services: Factory owning support services and their cleanup.
    """

    redirect1 = services.origin("redirect1")
    redirect2 = services.origin("redirect2")
    destination = services.origin("destination")
    request = lambda path: {
        "headers": f"POST /{path} HTTP/1.1\r\nHost: *\r\nContent-Length: {REDIRECT_POST__body_size}\r\n\r\n",
        "body": "",
    }
    redirect1.add_response(
        request("redirect1"),
        {
            "headers": f"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{redirect2.port}/redirect2\r\n\r\n",
            "body": "",
        },
    )
    redirect2.add_response(
        request("redirect2"),
        {
            "headers": f"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{destination.port}/redirectDest\r\n\r\n",
            "body": "",
        },
    )
    destination.add_response(
        request("redirectDest"),
        {
            "headers": "HTTP/1.1 204 No Content\r\n\r\n",
            "body": ""
        },
    )
    return redirect1, redirect2, destination


def configure_ats(ats_factory: ATSFactory, *, _redirect1: OriginServer) -> ATS:
    """Retain large POST bodies while following self redirects.

    :param _redirect1: Test-local redirect1 configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.http.number_of_redirections": 99,
            "proxy.config.http.post_copy_size": 919430601,
            "proxy.config.http.redirect.actions": "self:follow",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
    ats.remap_config.add_line(f"map http://127.0.0.1:{ats.http_port} http://127.0.0.1:{_redirect1.port}")
    return ats


def test_redirect_post(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS replays a large POST body while following multiple redirects.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _redirect1, _redirect2, _destination = configure_origins(services)
    _ats = configure_ats(ats_factory, _redirect1=_redirect1)

    for origin in (_redirect1, _redirect2, _destination):
        origin.start()
    _ats.start()
    upload = _ats.run_directory / "largefile.txt"
    with upload.open("wb") as stream:
        stream.truncate(REDIRECT_POST__body_size)
    result = curl.run_for(
        _ats,
        (f"--header Expect: --include --form 'filename=@{upload}' "
         f"'http://127.0.0.1:{_ats.http_port}/redirect1'"),
        timeout=20,
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 204 No Content" in result.stdout
