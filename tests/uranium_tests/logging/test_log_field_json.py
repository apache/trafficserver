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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines


class JsonLogFieldScenario:
    """Exercise JSON escaping and slicing with unusual request-header bytes."""

    REQUESTS = (
        ("/test-1", "test-1", "ab\td/ef"),
        ("/test-2", "test-2", "ab\x1fd/ef"),
        ("/test-3", "test-3", "abc\x7fde"),
        ("/test-4", "test-2", "ab\x80d/ef"),
    )

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._services = services
        self._curl = curl
        self._gold = Path(__file__).parent / "gold" / "field-json-test.gold"
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Create responses for the request paths used by the byte cases."""

        origin = services.origin("origin")
        for index in range(1, 5):
            origin.add_response(
                {
                    "headers": f"GET /test-{index} HTTP/1.1\r\nHost: test-{index if index < 4 else 2}\r\n\r\n",
                    "body": ""
                },
                {
                    "headers": "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n",
                    "body": f"Test {index}",
                },
            )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure JSON log escaping and a sliced Foo header field."""

        ats = ats_factory.create("ts", enable_cache=False)
        ats.records.update({"proxy.config.net.connections_throttle": 100})
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}/")
        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats":
                            [
                                {
                                    "name": "custom",
                                    "escape": "json",
                                    "format": '{"foo":"%<{Foo}cqh>","foo-slice":"%<{Foo}cqh[2:-3]>"}',
                                }
                            ],
                        "logs": [{
                            "filename": "field-json-test",
                            "format": "custom"
                        }],
                    }
            })
        return ats

    def send_requests(self) -> None:
        """Send the four header-byte cases with curl's argument fidelity."""

        for path, host, value in self.REQUESTS:
            result = self._curl.get(self._ats, path, headers={"Host": host, "Foo": value}, options=f"--verbose")
            assert result.returncode == 0, result.output

    def run(self) -> None:
        """Generate the JSON log and compare its escaped representation."""

        self._origin.start()
        self._ats.start()
        self.send_requests()
        path = self._ats.log_directory / "field-json-test.log"
        content = wait_for_file_lines(path, r'^\{"foo":', len(self.REQUESTS), timeout=30)
        assert_matches_gold(content, self._gold)


def test_log_field_json(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """JSON log fields escape and slice control and high-bit header bytes."""

    JsonLogFieldScenario(ats_factory, services, curl).run()
