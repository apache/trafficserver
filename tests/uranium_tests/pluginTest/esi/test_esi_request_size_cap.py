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


class EsiRequestSizeCapScenario:
    """Render an ESI document whose include URL exceeds MAX_REQ_LEN."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve a document containing an oversized ESI include URL."""

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

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable ESI processing and its diagnostic tag."""

        ats = ats_factory.create("ts")
        if not ats.plugin_exists("esi.so"):
            pytest.skip("esi.so is required")
        ats.plugin_config.add_line("esi.so")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|plugin_esi",
        })
        ats.remap_config.add_line(f"map http://www.example.com/ http://127.0.0.1:{self._origin.port}")
        return ats

    @staticmethod
    def verify_client(result: CommandResult) -> None:
        """Require the outer document request itself to complete."""

        assert result.returncode == 0, result.output

    def run(self) -> None:
        """Request the ESI document and require the size-cap diagnostic."""

        self._origin.start()
        self._ats.start()
        result = self._curl.get(
            self._ats,
            "/oversized.php",
            headers={
                "Host": "www.example.com",
                "Accept": "*/*"
            },
            options=f"--output /dev/null --silent",
        )
        self.verify_client(result)
        wait_for_file_lines(self._ats.diags_log, r"HTTP request size exceeds maximum 32768", 1)


def test_esi_request_size_cap(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ESI refuses include requests whose serialized request exceeds 32 KiB."""

    EsiRequestSizeCapScenario(ats_factory, services, curl).run()
