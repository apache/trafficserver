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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory, wait_for_file_lines

MAX_REQUEST_LENGTH = 32 * 1024


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Serve a document containing an oversized ESI include URL.

    :param services: Factory owning support services and their cleanup.
    """

    oversized_path = "A" * (MAX_REQUEST_LENGTH + 1)
    body = (
        "<html>\n<body>\n"
        f'<p>Hello, <esi:include src="http://www.example.com/{oversized_path}"/></p>\n'
        "</body>\n</html>\n")
    origin = services.origin("origin")
    origin.add_response(
        {
            "headers": ("GET /oversized.php HTTP/1.1\r\n"
                        "Host: www.example.com\r\nContent-Length: 0\r\n\r\n"),
            "body": "",
        },
        {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nX-Esi: 1\r\n"
                    f"Connection: close\r\nContent-Length: {len(body)}\r\nCache-Control: max-age=300\r\n\r\n"),
            "body": body,
        },
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Enable ESI processing and its diagnostic tag.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("esi.so"):
        pytest.skip("esi.so is required")
    ats.plugin_config.add_line("esi.so")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|plugin_esi",
    })
    ats.remap_config.add_line(f"map http://www.example.com/ http://127.0.0.1:{_origin.port}")
    return ats


def verify_client(result: CommandResult) -> None:
    """Require the outer document request itself to complete.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output


def test_esi_request_size_cap(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ESI refuses include requests whose serialized request exceeds 32 KiB.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.get(
        _ats,
        "/oversized.php",
        headers={
            "Host": "www.example.com",
            "Accept": "*/*"
        },
        options=f"--output /dev/null --silent",
    )
    verify_client(result)
    wait_for_file_lines(_ats.diags_log, r"HTTP request size exceeds maximum 32768", 1)
