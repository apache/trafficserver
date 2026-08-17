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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, send_tcp

TEST_DIRECTORY = Path(__file__).parent
SECOND_REQUEST = "GET / HTTP/1.1\r\nHost: boa\r\n\r\n"


class GoodRequestAfterBadScenario:
    """Verify an invalid request cannot leak a following pipelined request."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._strict = self.configure_ats(ats_factory, "strict", 1)
        self._less_strict = self.configure_ats(ats_factory, "less-strict", 2)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Create the response used by accepted requests."""

        origin = services.origin("origin")
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nConnection: close\r\nLast-Modified: Tue, 08 May 2018 15:49:41 GMT\r\n"
                        "Cache-Control: max-age=1000\r\n\r\n"),
                "body": "xxx",
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, name: str, strictness: int) -> ATS:
        """Configure one strict URI parsing policy."""

        ats = ats_factory.create(name)
        ats.records.update({"proxy.config.http.strict_uri_parsing": strictness})
        ats.remap_config.add_lines(
            (
                f"map / http://127.0.0.1:{self._origin.http_port}",
                f"map /bob<> http://127.0.0.1:{self._origin.http_port}",
            ))
        return ats

    @staticmethod
    def assert_gold(response: str, filename: str) -> None:
        """Compare one raw response with its wildcard gold file."""

        assert_matches_gold(response, TEST_DIRECTORY / "gold" / filename)

    def send_bad_requests(self) -> None:
        """Exercise malformed headers, methods, bodies, and request lines."""

        cases = (
            ("GET / HTTP/1.1\r\nHost : bob\r\n\r\n", "bad_good_request.gold"),
            ("GET / HTTP/11.1\r\nhost: bob\r\n\r\n", "bad_protocol_number.gold"),
            ("GET / HTTP/1.1\r\nhost: bob\r\ntransfer-encoding: random\r\n\r\n", "bad_te_value.gold"),
            (
                "GET / HTTP/1.1\r\nhost: bob\r\ntransfer-encoding: \x08chunked\r\n\r\n",
                "invalid_character_in_te_value.gold",
            ),
            ("GET / HTTP/1.1\r\nhost: bob\r\ncontent-length:+3\r\n\r\n", "bad_good_request_header.gold"),
            ("GET / HTTP/1.1\r\nhost: bob\r\ncontent-length:\x0c3\r\n\r\n", "bad_good_request_header.gold"),
            ("TRACE /foo HTTP/1.1\r\nHost: bob\r\nContent-length:2\r\n\r\nok", "bad_good_request.gold"),
            (
                "TRACE /foo HTTP/1.1\r\nHost: bob\r\ntransfer-encoding: chunked\r\n\r\n2\r\nokG",
                "bad_good_request.gold",
            ),
            ("gET / HTTP/1.1\r\nHost:bob\r\n\r\n", "bad_method.gold"),
            ("GET / HTTP/1.1\r\nHost:bob\r\n \r\n", "bad_good_request.gold"),
            ("GET /bob<> HTTP/1.1\r\nhost: bob\r\n\r\n", "bad_good_request_http1.gold"),
            ("GET /bob foo HTTP/1.1\r\nhost: bob\r\n\r\n", "bad_good_request_http1.gold"),
            ("GET / HTP/1.1\r\nhost: bob\r\n\r\n", "bad_good_request_http1.gold"),
        )
        for request, gold in cases:
            self.assert_gold(send_tcp(self._strict.http_port, request + SECOND_REQUEST), gold)

    def send_curl_trace_requests(self) -> None:
        """Exercise TRACE body validation through curl."""

        result = self._curl.run_for(
            self._strict,
            (
                f"--verbose --http1.1 --header 'Transfer-Encoding: chunked' --data aaa -X TRACE "
                f"'http://127.0.0.1:{self._strict.http_port}/foo'"),
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 400 Invalid HTTP Request" in result.output
        assert "<TITLE>Bad Request</TITLE>" in result.stdout
        assert "Description: Could not process this request." in result.stdout

        result = self._curl.run_for(
            self._strict,
            f"--verbose --http1.1 -X TRACE 'http://127.0.0.1:{self._strict.http_port}/bar'",
        )
        assert result.returncode == 0, result.output
        assert "HTTP/1.1 501 Unsupported method ('TRACE')" in result.output

    def send_less_strict_requests(self) -> None:
        """Verify strictness level two accepts only the intended URL case."""

        accepted = send_tcp(
            self._less_strict.http_port,
            "GET /bob<> HTTP/1.1\r\nhost: bob\r\n\r\n" + SECOND_REQUEST,
        )
        assert "HTTP/1.1 200 OK" in accepted
        for request in (
                "GET /bob foo HTTP/1.1\r\nhost: bob\r\n\r\n",
                "GET / HTP/1.1\r\nhost: bob\r\n\r\n",
        ):
            self.assert_gold(send_tcp(self._less_strict.http_port, request + SECOND_REQUEST), "bad_good_request_http1.gold")

    def run(self) -> None:
        """Run control traffic followed by the malformed request matrix."""

        self._origin.start()
        self._strict.start()
        self._less_strict.start()
        assert "HTTP/1.1 200 OK" in send_tcp(self._strict.http_port, "GET / HTTP/1.1\r\nHost: bob\r\n\r\n")
        assert "HTTP/1.1 200 OK" in send_tcp(self._less_strict.http_port, "GET / HTTP/1.1\r\nHost: bob\r\n\r\n")
        self.send_bad_requests()
        self.send_curl_trace_requests()
        self.send_less_strict_requests()


def test_good_request_after_bad(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A malformed request terminates processing before a pipelined request."""

    GoodRequestAfterBadScenario(ats_factory, services, curl).run()
