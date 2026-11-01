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
"""Every request forwarded to an HTTP/2 origin ends its stream, however the client ended its request."""
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin

SCRIPT = Path(__file__).with_name("h2_request_end_stream.py")

# Each case: id, method, how the client ends its stream, Content-Length, body, and the
# frame that should carry END_STREAM to the origin.  END_STREAM goes on HEADERS
# whenever no body follows them, including for a declared zero length.
CASES = [
    ("get-headers", "GET", "headers", None, "", "headers"),
    ("options-headers", "OPTIONS", "headers", None, "", "headers"),
    ("post-body", "POST", "data", 5, "hello", "data"),
    ("post-body-no-length", "POST", "data", None, "hello", "data"),
    ("post-late-body-no-length", "POST", "late", None, "hello", "data"),
    ("post-zero-length-headers", "POST", "headers", 0, "", "headers"),
    ("options-zero-length-empty-data", "OPTIONS", "data", 0, "", "headers"),
    ("get-zero-length-empty-data", "GET", "data", 0, "", "headers"),
    ("post-zero-length-empty-data", "POST", "data", 0, "", "headers"),
    ("options-empty-data", "OPTIONS", "data", None, "", "data"),
    ("options-late-empty-data", "OPTIONS", "late", None, "", "data"),
    ("post-headers-no-length", "POST", "headers", None, "", "headers"),
]

# Each case: id, method, body framing, body, whether the end of the body arrives
# after a pause, and the frame that should carry END_STREAM to the origin.
H1_CASES = [
    ("post-length", "POST", "length", "hello", False, "data"),
    ("post-zero-length", "POST", "length", "", False, "headers"),
    ("post-chunked", "POST", "chunked", "hello", False, "data"),
    ("post-chunked-empty", "POST", "chunked", "", False, "data"),
    ("patch-chunked", "PATCH", "chunked", "hello", False, "data"),
    ("post-length-late", "POST", "length", "hello", True, "data"),
    ("post-chunked-late", "POST", "chunked", "hello", True, "data"),
    ("post-chunked-empty-late", "POST", "chunked", "", True, "data"),
]

# A request header block this large is well over the 16 KB ATS puts in one HEADERS
# frame, so ATS sends it to the origin in HEADERS and CONTINUATION frames.  It still
# fits the default request header limits.
LARGE_HEADER_SIZE = 30000

LARGE_HEADER_CASES = [
    ("get-headers", "GET", "headers", None, "", "headers"),
    ("options-empty-data", "OPTIONS", "data", None, "", "data"),
    ("post-zero-length-empty-data", "POST", "data", 0, "", "headers"),
    ("post-body", "POST", "data", 5, "hello", "data"),
]

H1_LARGE_HEADER_CASES = [
    ("get", "GET", "length", "", False, "headers"),
    ("post-length", "POST", "length", "hello", False, "data"),
    ("post-chunked", "POST", "chunked", "hello", False, "data"),
]


def start_proxy(ats_factory: ATSFactory, services: ServiceFactory) -> ATS:
    """Start an origin that replies only to ended streams, and a proxy that gives up on it quickly.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    """
    port = services.allocate_port()
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin",
        [sys.executable, SCRIPT, "serve", "127.0.0.1",
         str(port), "--cert", ssl / "server.pem", "--key", ssl / "server.key"],
        ready_port=port)
    ats = configure_h2_origin(
        ats_factory,
        port,
        **{
            # A request the origin never sees end turns into a 504 in seconds.
            "proxy.config.http.transaction_no_activity_timeout_out": 2,
            "proxy.config.http.connect_attempts_max_retries": 0,
        })
    origin.start()
    ats.start()
    return ats


def run_client(services: ServiceFactory, command: list[object]) -> None:
    """Run one client and check that it reported a complete exchange with the origin.

    :param services: Custom origin and client factory.
    :param command: Client command line.
    """
    result = services.process("client", command).run(timeout=30)
    assert result.returncode == 0, result.output
    assert "PASS" in result.stdout


def h2_command(ats: ATS, method: str, end: str, content_length: int | None, body: str, end_frame: str, *options:
               str) -> list[object]:
    """Build the HTTP/2 client command line.

    :param ats: Running proxy.
    :param method: Request method.
    :param end: How the client ends its request stream, one of the helper's ENDINGS.
    :param content_length: Content-Length the client sends, if any.
    :param body: Request body the client sends.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    :param options: Further client options.
    """
    command = [
        sys.executable, SCRIPT, "request", "127.0.0.1",
        str(ats.https_port), method, "--end", end, "--body", body, "--origin-end-frame", end_frame, *options
    ]
    if content_length is not None:
        command += ["--content-length", str(content_length)]
    return command


def h1_command(ats: ATS, method: str, framing: str, body: str, late: bool, end_frame: str, *options: str) -> list[object]:
    """Build the HTTP/1.1 client command line.

    :param ats: Running proxy.
    :param method: Request method.
    :param framing: Content-Length or chunked body framing.
    :param body: Request body the client sends.
    :param late: Whether the end of the body arrives after a pause.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    :param options: Further client options.
    """
    command = [
        sys.executable, SCRIPT, "request-h1", "127.0.0.1",
        str(ats.http_port), method, "--framing", framing, "--body", body, "--origin-end-frame", end_frame, *options
    ]
    if late:
        command.append("--late")
    return command


@pytest.mark.parametrize("method,end,content_length,body,end_frame", [pytest.param(*case[1:], id=case[0]) for case in CASES])
def test_h2_origin_request_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, end: str, content_length: int | None, body: str,
        end_frame: str) -> None:
    """The origin receives the end of an HTTP/2 client request and the client gets its reply.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param end: How the client ends its request stream.
    :param content_length: Content-Length the client sends, if any.
    :param body: Request body the client sends.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    """
    ats = start_proxy(ats_factory, services)
    run_client(services, h2_command(ats, method, end, content_length, body, end_frame))
    assert ats.is_running


@pytest.mark.parametrize("method,framing,body,late,end_frame", [pytest.param(*case[1:], id=case[0]) for case in H1_CASES])
def test_h1_client_request_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, framing: str, body: str, late: bool,
        end_frame: str) -> None:
    """The origin receives all of an HTTP/1.1 client request body and the end of its stream.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param framing: Content-Length or chunked body framing.
    :param body: Request body the client sends.
    :param late: Whether the end of the body arrives after a pause.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    """
    ats = start_proxy(ats_factory, services)
    run_client(services, h1_command(ats, method, framing, body, late, end_frame))
    assert ats.is_running


@pytest.mark.parametrize(
    "method,end,content_length,body,end_frame", [pytest.param(*case[1:], id=case[0]) for case in LARGE_HEADER_CASES])
def test_h2_client_large_header_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, end: str, content_length: int | None, body: str,
        end_frame: str) -> None:
    """The origin receives the end of an HTTP/2 client request whose header block ATS sends in HEADERS and CONTINUATION frames.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param end: How the client ends its request stream.
    :param content_length: Content-Length the client sends, if any.
    :param body: Request body the client sends.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    """
    ats = start_proxy(ats_factory, services)
    run_client(services, h2_command(ats, method, end, content_length, body, end_frame, "--header-size", str(LARGE_HEADER_SIZE)))
    assert ats.is_running


@pytest.mark.parametrize(
    "method,framing,body,late,end_frame", [pytest.param(*case[1:], id=case[0]) for case in H1_LARGE_HEADER_CASES])
def test_h1_client_large_header_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, framing: str, body: str, late: bool,
        end_frame: str) -> None:
    """The origin receives the end of an HTTP/1.1 client request whose header block ATS sends in HEADERS and CONTINUATION frames.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param framing: Content-Length or chunked body framing.
    :param body: Request body the client sends.
    :param late: Whether the end of the body arrives after a pause.
    :param end_frame: Frame that should carry END_STREAM to the origin.
    """
    ats = start_proxy(ats_factory, services)
    run_client(services, h1_command(ats, method, framing, body, late, end_frame, "--header-size", str(LARGE_HEADER_SIZE)))
    assert ats.is_running


def test_h2_client_trace_body_end_stream(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A TRACE request reaches the origin ended and without the body its client sent.

    ATS never forwards a TRACE body, so nothing but the HEADERS frame can end the
    origin stream.  An HTTP/2 client without Content-Length is the one way a TRACE
    body gets this far: with a nonzero Content-Length, or a chunked HTTP/1.1 body,
    ATS answers 400.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    """
    ats = start_proxy(ats_factory, services)
    run_client(services, h2_command(ats, "TRACE", "data", None, "hello", "headers", "--origin-body-bytes", "0"))
    assert ats.is_running
