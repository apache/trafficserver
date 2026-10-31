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

CASES = [
    ("get-headers", "GET", "headers", None, ""),
    ("options-headers", "OPTIONS", "headers", None, ""),
    ("post-body", "POST", "data", 5, "hello"),
    ("post-body-no-length", "POST", "data", None, "hello"),
    ("post-zero-length-headers", "POST", "headers", 0, ""),
    ("options-zero-length-empty-data", "OPTIONS", "data", 0, ""),
    ("get-zero-length-empty-data", "GET", "data", 0, ""),
    ("post-zero-length-empty-data", "POST", "data", 0, ""),
    ("options-empty-data", "OPTIONS", "data", None, ""),
    ("options-late-empty-data", "OPTIONS", "late", None, ""),
    ("post-headers-no-length", "POST", "headers", None, ""),
]

H1_CASES = [
    ("post-length", "POST", "length", "hello"),
    ("post-zero-length", "POST", "length", ""),
    ("post-chunked", "POST", "chunked", "hello"),
    ("post-chunked-empty", "POST", "chunked", ""),
    ("patch-chunked", "PATCH", "chunked", "hello"),
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
            # Each request opens its own origin connection, so the origin's handshake
            # delay applies to every request.
            "proxy.config.http.server_session_sharing.match": "none",
        })
    origin.start()
    ats.start()
    return ats


@pytest.mark.parametrize("method,end,content_length,body", [pytest.param(*case[1:], id=case[0]) for case in CASES])
def test_h2_origin_request_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, end: str, content_length: int | None, body: str) -> None:
    """The origin receives the end of an HTTP/2 client request and the client gets its reply.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param end: Frame that carries the client's END_STREAM.
    :param content_length: Content-Length the client sends, if any.
    :param body: Request body the client sends.
    """
    ats = start_proxy(ats_factory, services)
    command = [sys.executable, SCRIPT, "request", "127.0.0.1", str(ats.https_port), method, "--end", end, "--body", body]
    if content_length is not None:
        command += ["--content-length", str(content_length)]
    result = services.process("client", command).run(timeout=30)
    assert result.returncode == 0, result.output
    assert "PASS" in result.stdout
    assert ats.is_running


@pytest.mark.parametrize("method,framing,body", [pytest.param(*case[1:], id=case[0]) for case in H1_CASES])
def test_h1_client_request_end_stream(
        ats_factory: ATSFactory, services: ServiceFactory, method: str, framing: str, body: str) -> None:
    """The origin receives all of an HTTP/1.1 client request body and the end of its stream.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param method: Request method.
    :param framing: Content-Length or chunked body framing.
    :param body: Request body the client sends.
    """
    ats = start_proxy(ats_factory, services)
    command = [sys.executable, SCRIPT, "request-h1", "127.0.0.1", str(ats.http_port), method, "--framing", framing, "--body", body]
    result = services.process("client", command).run(timeout=30)
    assert result.returncode == 0, result.output
    assert "PASS" in result.stdout
    assert ats.is_running
