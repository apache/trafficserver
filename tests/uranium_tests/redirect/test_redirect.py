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
import socket

from tools.uranium.services import (
    ATS,
    ATSFactory,
    DNSServer,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent

REDIRECT__statuses = {
    301: "Moved Permanently",
    302: "Found",
    303: "See Other",
    305: "Use Proxy",
    307: "Temporary Redirect",
    308: "Permanent Redirect",
}


def configure_origins(services: ServiceFactory, *, _destination: OriginServer) -> OriginServer:
    """Create absolute, relative, and status-specific redirect responses.

    :param _destination: Test-local destination configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    redirect = services.origin("redirect")
    final = {"headers": "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", "body": ""}
    _destination.add_response(
        {
            "headers": "GET /redirectDest HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        final,
    )
    redirect.add_response(
        {
            "headers": "GET /redirect HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers": (f"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{_destination.port}/redirectDest\r\n\r\n"),
            "body": "",
        },
    )
    redirect.add_response(
        {
            "headers": "GET /redirect-relative-path HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 302 Found\r\nLocation: /redirect\r\n\r\n",
            "body": ""
        },
    )
    redirect.add_response(
        {
            "headers": "GET /redirect HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        final,
    )
    redirect.add_response(
        {
            "headers": "GET /redirect-relative-path-no-leading-slash HTTP/1.1\r\nHost: *\r\n\r\n",
            "body": ""
        },
        {
            "headers": "HTTP/1.1 302 Found\r\nLocation: redirect\r\n\r\n",
            "body": ""
        },
    )
    for status, phrase in REDIRECT__statuses.items():
        redirect.add_response(
            {
                "headers": f"GET /redirect{status} HTTP/1.1\r\nHost: *\r\n\r\n",
                "body": ""
            },
            {
                "headers": (f"HTTP/1.1 {status} {phrase}\r\nConnection: close\r\nLocation: /redirect\r\n\r\n"),
                "body": "",
            },
        )
    return redirect


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve the client-facing redirect hostname to loopback.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({"iwillredirect.test": ["127.0.0.1"]})
    return dns


def configure_ats(ats_factory: ATSFactory, *, _dns: DNSServer) -> ATS:
    """Follow one self redirect and log the cache redirect subcode.

    :param _dns: Test-local dns configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|dns|redirect",
            "proxy.config.http.number_of_redirections": 1,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.url_remap.remap_required": 0,
            "proxy.config.http.redirect.actions": "self:follow",
        })
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "custom",
                        "format": "client_url=%<pqu> cache_result: code=%<crc> subcode=%<crsc>",
                    }],
                    "logs": [{
                        "filename": "the_log",
                        "format": "custom"
                    }],
                }
        })
    return ats


def _request(path: str, *, _ats: ATS, _redirect: OriginServer) -> str:
    """Send one raw request and return the response headers.

    :param _ats: Test-local ats configured by the test.
    :param _redirect: Test-local redirect configured by the test.
    :param path: Resource or file path used by this operation.
    """

    request = (f"GET {path} HTTP/1.1\r\nHost: iwillredirect.test:{_redirect.port}\r\n"
               "Connection: close\r\n\r\n").encode()
    chunks = []
    with socket.create_connection(("127.0.0.1", _ats.http_port), timeout=5) as connection:
        connection.sendall(request)
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks).decode(errors="replace")


def test_redirect(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """ATS follows absolute, relative, and status-specific redirects.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _destination = services.origin("destination")
    _redirect = configure_origins(services, _destination=_destination)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _dns=_dns)

    _destination.start()
    _redirect.start()
    _dns.start()
    _ats.start()
    paths = ["/redirect", "/redirect-relative-path", "/redirect-relative-path-no-leading-slash"]
    paths.extend(f"/redirect{status}" for status in REDIRECT__statuses)
    for path in paths:
        response = _request(path, _ats=_ats, _redirect=_redirect)
        assert response.startswith("HTTP/1.1 204 No Content"), response

    log_path = _ats.log_directory / "the_log.log"
    log = wait_for_file_lines(log_path, "client_url=", 17, timeout=20)
    normalized = log.replace(f":{_redirect.port}", ":PORT")
    assert_matches_gold(normalized, TEST_DIRECTORY / "gold" / "redirect_log.gold")
