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
"""Validate DATA lengths without narrowing Content-Length or underflowing padding."""
import re
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory

REJECTED = ("cl-wraps-to-zero", "cl-wraps-to-payload-length", "cl-larger-than-payload", "cl-negative")
PADDING = ("padded-pad-length-equals-frame-length", "padded-pad-length-equals-frame-length-then-body", "padded-empty-frame")
PROXIED = ("cl-matches", "padded-matches", "padded-no-data-then-body-matches")


@pytest.mark.parametrize("case", REJECTED + PADDING + PROXIED)
def test_http2_content_length_validation(ats_factory: ATSFactory, services: ServiceFactory, case: str) -> None:
    """Reject malformed lengths and padding while forwarding valid padded bodies.

    :param ats_factory: TLS proxy factory.
    :param services: Origin factory.
    :param case: Malformed or valid HTTP/2 request scenario to exercise.
    """
    origin = services.origin("origin")
    if case in PROXIED:
        origin.add_response(
            {
                "headers": f"POST /{case} HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 3\r\n\r\n",
                "body": "abc"
            }, {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"})
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, disable_log_checks=True)
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "http2"})
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
    origin.start()
    ats.start()
    result = ats.run(sys.executable, Path(__file__).parent / "clients/h2_content_length_client.py", str(ats.https_port), case)
    assert result.returncode == 0, result.output
    expression = (
        r"Received (RST_STREAM|GOAWAY|HTTP/2 response with status 4\d\d)" if case in REJECTED else
        r"Received GOAWAY with error code 1\b" if case in PADDING else "Received HTTP/2 response with status 200")
    assert re.search(expression, result.stdout), result.output
    ats.stop()
    log = ats.diags_log.read_text()
    if case == "cl-wraps-to-zero":
        assert "Bad payload length content_length=4294967296 data_length=3" in log
    allowed = r"HTTP/2 connection error code=0x01 .* recv data pad >= payload"
    if case in PADDING:
        assert re.search(allowed, log), log
    allowed_stream = r"HTTP/2 stream error code=0x01 .* recv data bad payload length"
    expected_errors = [allowed] if case in PADDING else [allowed_stream] if case in REJECTED else []
    if case == "cl-negative":
        # ATS can reject the headers before processing the queued DATA. That
        # DATA then legitimately encounters the already closed stream.
        expected_errors.append(r"HTTP/2 stream error code=0x05 .* stream_id=1 recv data stream closed")
    for line in log.splitlines():
        if "ERROR: " in line:
            message = line.split("ERROR: ", 1)[1]
            assert any(re.fullmatch(pattern, message) for pattern in expected_errors), line
    assert not re.search(r"FATAL:|Unrecognized configuration value", log), log
