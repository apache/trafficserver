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


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the response used by accepted requests.

    :param services: Factory owning support services and their cleanup.
    """

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


def configure_ats(ats_factory: ATSFactory, name: str, strictness: int, *, _origin: OriginServer) -> ATS:
    """Configure one strict URI parsing policy.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param strictness: Strictness used by this test step.
    """

    ats = ats_factory.create(name)
    ats.records.update({"proxy.config.http.strict_uri_parsing": strictness})
    ats.remap_config.add_lines(
        (
            f"map / http://127.0.0.1:{_origin.http_port}",
            f"map /bob<> http://127.0.0.1:{_origin.http_port}",
        ))
    return ats


def assert_gold(response: str, filename: str) -> None:
    """Compare one raw response with its wildcard gold file.

    :param response: Response used by this test step.
    :param filename: Filename used by this test step.
    """

    assert_matches_gold(response, TEST_DIRECTORY / "gold" / filename)


def send_bad_requests(*, _strict: ATS) -> None:
    """Exercise malformed headers, methods, bodies, and request lines.

    :param _strict: Test-local strict configured by the test.
    """

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
        assert_gold(send_tcp(_strict.http_port, request + SECOND_REQUEST), gold)


def send_curl_trace_requests(*, _curl: Curl, _strict: ATS) -> None:
    """Exercise TRACE body validation through curl.

    :param _curl: Test-local curl configured by the test.
    :param _strict: Test-local strict configured by the test.
    """

    result = _curl.run_for(
        _strict,
        (
            f"--verbose --http1.1 --header 'Transfer-Encoding: chunked' --data aaa -X TRACE "
            f"'http://127.0.0.1:{_strict.http_port}/foo'"),
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 400 Invalid HTTP Request" in result.output
    assert "<TITLE>Bad Request</TITLE>" in result.stdout
    assert "Description: Could not process this request." in result.stdout

    result = _curl.run_for(
        _strict,
        f"--verbose --http1.1 -X TRACE 'http://127.0.0.1:{_strict.http_port}/bar'",
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 501 Unsupported method ('TRACE')" in result.output


def send_less_strict_requests(*, _less_strict: ATS) -> None:
    """Verify strictness level two accepts only the intended URL case.

    :param _less_strict: Test-local less strict configured by the test.
    """

    accepted = send_tcp(
        _less_strict.http_port,
        "GET /bob<> HTTP/1.1\r\nhost: bob\r\n\r\n" + SECOND_REQUEST,
    )
    assert "HTTP/1.1 200 OK" in accepted
    for request in (
            "GET /bob foo HTTP/1.1\r\nhost: bob\r\n\r\n",
            "GET / HTP/1.1\r\nhost: bob\r\n\r\n",
    ):
        assert_gold(send_tcp(_less_strict.http_port, request + SECOND_REQUEST), "bad_good_request_http1.gold")


def test_good_request_after_bad(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A malformed request terminates processing before a pipelined request.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _strict = configure_ats(ats_factory, "strict", 1, _origin=_origin)
    _less_strict = configure_ats(ats_factory, "less-strict", 2, _origin=_origin)

    _origin.start()
    _strict.start()
    _less_strict.start()
    assert "HTTP/1.1 200 OK" in send_tcp(_strict.http_port, "GET / HTTP/1.1\r\nHost: bob\r\n\r\n")
    assert "HTTP/1.1 200 OK" in send_tcp(_less_strict.http_port, "GET / HTTP/1.1\r\nHost: bob\r\n\r\n")
    send_bad_requests(_strict=_strict)
    send_curl_trace_requests(_curl=curl, _strict=_strict)
    send_less_strict_requests(_less_strict=_less_strict)
