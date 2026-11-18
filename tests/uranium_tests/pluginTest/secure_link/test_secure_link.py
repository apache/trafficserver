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
"""Verify the secure_link example plugin accepts a correct token in strict mode."""

import hashlib
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

SECRET = "uranium-secret"
CLIENT_IP = "127.0.0.1"
DIRECTORY = "secure/"
FILE_PATH = f"/{DIRECTORY}file.txt"


def configure_server(*, _services: ServiceFactory) -> OriginServer:
    """Create the origin response for the protected file.

    :param _services: Test-local services configured by the test.
    """

    origin = _services.origin("origin")
    origin.add_response(
        {"headers": f"GET {FILE_PATH} HTTP/1.1\r\nHost: secure.link\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "secured"},
    )
    return origin


def configure_ats(*, _ats_factory: ATSFactory, _origin: OriginServer) -> ATS:
    """Map a host through secure_link in strict mode.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    ats = _ats_factory.create("ats", enable_cache=False, disable_log_checks=True)
    if not ats.plugin_exists("secure_link.so"):
        pytest.skip("secure_link.so is not installed")
    plugin = f"@plugin=secure_link.so @pparam=secret:{SECRET} @pparam=policy:strict"
    ats.remap_config.add_lines((f"map http://secure.link/ http://127.0.0.1:{_origin.port}/ {plugin}",))
    return ats


def token_for(expire: str) -> str:
    """Return the token the plugin expects for the protected file.

    The plugin hashes the secret, the client address, the request path up to
    and including its last slash, and the expiry string, in that order.

    :param expire: Expiry time as the hex string sent in the request.
    """

    return hashlib.md5(f"{SECRET}{CLIENT_IP}{DIRECTORY}{expire}".encode()).hexdigest()


def request_status(*, _ats: ATS, _curl: Curl, token: str, expire: str) -> str:
    """Send one request through the proxy and return its status line.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param token: Value of the st query parameter.
    :param expire: Value of the ex query parameter.
    """

    url = f"http://secure.link{FILE_PATH}?st={token}&ex={expire}"
    result = _curl.run_for(_ats, f"--verbose --proxy 'http://127.0.0.1:{_ats.http_port}' '{url}'")
    assert result.returncode == 0, result.output
    return next((line for line in result.stderr.splitlines() if line.startswith("< HTTP/1.1 ")), result.output)


def test_secure_link(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Accept a correctly computed token and reject a wrong one.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    if Curl(ats_factory.run_directory).uses_uds:
        pytest.skip("secure_link tokens bind the client IP address")
    _origin = configure_server(_services=services)
    _ats = configure_ats(_ats_factory=ats_factory, _origin=_origin)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _ats.start()
    expire = format(int(time.time()) + 3600, "x")
    valid = token_for(expire)
    wrong = valid[::-1]
    assert wrong != valid
    status = request_status(_ats=_ats, _curl=_curl, token=valid, expire=expire)
    assert status.startswith("< HTTP/1.1 200 "), f"valid token: {status}"
    status = request_status(_ats=_ats, _curl=_curl, token=wrong, expire=expire)
    assert status.startswith("< HTTP/1.1 403 "), f"wrong token: {status}"
