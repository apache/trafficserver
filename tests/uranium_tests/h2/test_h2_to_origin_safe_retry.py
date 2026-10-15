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
"""Retry only unprocessed HTTP/2 requests with a complete retained body."""
import re
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin, finish_origin, wait_for_metrics

CASES = [
    ("rst", "rst-refused", 1, 4096, True),
    ("goaway", "goaway-last-stream-zero", 1, 4096, True),
    ("goaway-reserved", "goaway-last-stream-zero", 1, 4096, True),
    *[(mode, "no-retry", 0, size, False) for mode in ("rst", "goaway") for size in (4096, 4)],
    *[
        (mode, "no-retry", 1, 4096, False)
        for mode in ("rst-internal", "rst-cancel", "goaway-equal", "response-rst", "unsolicited-continuation", "settings-flood")
    ],
    *[(mode, "goaway-drain", 1, 4096, False) for mode in ("goaway-drain", "goaway-drain-max")],
    ("goaway-drain-above", "goaway-last-stream-zero", 1, 4096, True),
    ("goaway-split", "goaway-split", 1, 4096, False),
    ("early-rst", "rst-refused", 1, 4096, True),
    ("early-rst", "no-retry", 0, 4096, False),
]


@pytest.mark.parametrize("mode,replay_key,buffering,copy_size,retry", CASES)
def test_h2_to_origin_safe_retry(
        ats_factory: ATSFactory, services: ServiceFactory, mode: str, replay_key: str, buffering: int, copy_size: int,
        retry: bool) -> None:
    """Assert retry admission separately from the final HTTP response.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    :param mode: Origin reset, GOAWAY, or invalid-frame behavior.
    :param replay_key: Transaction selected from the verifier replay.
    :param buffering: Whether ATS buffers the complete request.
    :param copy_size: Retained POST-body capacity in bytes.
    :param retry: Whether the request should be retried.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    invalid = mode in ("unsolicited-continuation", "settings-flood")
    ats = configure_h2_origin(
        ats_factory,
        port,
        disable_log_checks=invalid,
        **{
            "proxy.config.http.connect_attempts_max_retries": 3,
            "proxy.config.http.request_buffer_enabled": buffering,
            "proxy.config.http.post_copy_size": copy_size,
            "proxy.config.http.number_of_redirections": 1 if copy_size == 4 else 0,
            "proxy.config.http2.max_settings_frames_per_minute": 2,
        })
    if invalid:
        code = "01" if mode == "unsolicited-continuation" else "0b"
        reason = "unsolicited CONTINUATION frame" if code == "01" else "recv settings too frequent SETTINGS frames"
    stop = ats.run_directory / "stop-origin"
    ssl = Path(__file__).parents[2] / "tools/ssl"
    split = mode == "goaway-split"
    drain = mode in ("goaway-drain", "goaway-drain-max")
    attempts = 5 if split else 2 if retry else 1
    origin = services.process(
        "origin", [
            sys.executable, directory / "safe_retry_origin.py", mode,
            str(port), ssl / "server.pem", ssl / "server.key", stop,
            str(attempts)
        ],
        ready_port=port)
    origin.stdout.contains("request_received attempt=1", "Validate the origin's framing and request handling")
    if split:
        origin.stdout.contains("drain_succeeded attempts=3 method=POST", "Validate the origin's framing and request handling")
        origin.stdout.contains(
            "retry_succeeded attempts=5 method=POST body=request-body", "Validate the origin's framing and request handling")
    elif retry:
        origin.stdout.contains(
            "retry_succeeded attempts=2 method=POST body=request-body", "Validate the origin's framing and request handling")
    else:
        origin.stdout.excludes("request_received attempt=2", "Validate the origin's framing and request handling")
        if drain:
            origin.stdout.contains("drain_succeeded attempts=1 method=POST", "Validate the origin's framing and request handling")
    if drain or split or mode == "goaway-drain-above":
        origin.stdout.contains("drained_session_closed_by_peer", "Validate the origin's framing and request handling")
    if mode in ("rst", "early-rst", "rst-internal", "rst-cancel", "response-rst"):
        error = {"rst-internal": "INTERNAL_ERROR", "rst-cancel": "CANCEL"}.get(mode, "REFUSED_STREAM")
        origin.stdout.contains(f"action=RST_STREAM attempt=1 error={error}", "Validate the origin's framing and request handling")
    elif mode.startswith("goaway"):
        last_id = {
            "goaway-equal": "[1-9][0-9]*",
            "goaway-drain": "[1-9][0-9]*",
            "goaway-split": "[1-9][0-9]*",
            "goaway-drain-max": "2147483647"
        }.get(mode, "0")
        origin.stdout.contains(
            f"action=GOAWAY attempt=1 last_stream_id={last_id}", "Validate the origin's framing and request handling")
    if invalid:
        origin.stdout.contains(
            f"peer_goaway error={1 if mode == 'unsolicited-continuation' else 11}",
            "Validate the origin's framing and request handling")
    origin.start()
    ats.start()
    if split:
        result = services.process("client", [sys.executable, directory / "split_drain_client.py",
                                             str(ats.http_port)]).run(timeout=60)
        assert "Covered stream drained and uncovered streams retried" in result.stdout
    elif retry or drain:
        result = services.verifier_client(
            "client", directory / "replay_h2o_safe_retry/safe_retry.replay.yaml", http_ports=[ats.http_port],
            keys=replay_key).run(timeout=60)
    else:
        result = services.process("client", [sys.executable, directory / "negative_retry_client.py",
                                             str(ats.http_port)]).run(timeout=60)
    assert result.returncode == 0, result.output
    assert ats.is_running
    assert origin.is_running
    body_denied = not retry and mode in ("rst", "goaway", "early-rst")
    outcome = "admitted-twice" if split else "admitted" if retry else "body-denied" if body_denied else "not-admitted"
    wait_for_metrics(
        ats, f"h2o-retry-{outcome}.gold",
        ["proxy.process.http.origin.retry_admitted", "proxy.process.http.origin.retry_body_unavailable"])
    finish_origin(services, origin, stop)
    ats.stop()
    if invalid:
        diags = ats.diags_log.read_text(errors="replace")
        assert not re.search(f"ERROR:(?! HTTP/2 connection error code=0x{code}.*{reason})", diags), diags
        assert not re.search("FATAL:|Unrecognized configuration value", diags), diags
    if retry:
        assert "ERR_CLIENT_ABORT" not in ats.traffic_out.read_text(errors="replace")
