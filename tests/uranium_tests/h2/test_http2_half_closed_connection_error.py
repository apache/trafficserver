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
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, ProcessService, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
# Long enough that a DATA frame queued without a flush stays queued until the
# connection error, so only the FINI's own flush can write it.
WRITE_TIME_THRESHOLD_MS = 10000


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Shut sessions down gracefully and keep unflushed DATA frames queued.

    :param _origin_port: Port the client's origin listens on.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, disable_log_checks=True)
    if not ats.plugin_exists("header_rewrite.so"):
        pytest.skip("header_rewrite.so is not installed")
    rule = TEST_DIRECTORY / "rules" / "graceful_shutdown.conf"
    ats.copy_to_config(rule)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http2",
            "proxy.config.http2.write_time_threshold": WRITE_TIME_THRESHOLD_MS,
        }
    )
    ats.remap_config.add_line(
        f"map / http://127.0.0.1:{_origin_port} @plugin=header_rewrite.so @pparam={ats.config_directory / rule.name}"
    )
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _origin_port: int) -> ProcessService:
    """Build the frame-level HTTP/2 client that also runs the origin.

    :param _ats: Test-local ats configured by the test.
    :param _origin_port: Port the client's origin listens on.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (sys.executable, TEST_DIRECTORY / "half_closed_error_client.py", str(_ats.https_port), str(_origin_port)),
    )


def verify(result: CommandResult, *, _ats: ATS) -> None:
    """Require the queued DATA to reach the client before the session closes.

    :param _ats: Test-local ats configured by the test.
    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    # The session was half-closed, and sent no GOAWAY for the error.
    assert "before error: GOAWAY error_code=0 last_stream_id=1 " in result.stdout, result.output
    assert "after error: GOAWAY" not in result.stdout, result.output
    assert "second part delivered" in result.stdout, result.output
    wait_for_file_lines(_ats.diags_log, r"HTTP/2 connection error code=0x01 .*ping id not 0", 1)
    # The FINI found the DATA frame still queued, and it was written in time.
    wait_for_file_lines(_ats.traffic_out, r"waiting for queued frames to be written before closing", 1)
    assert "bytes still unsent" not in _ats.traffic_out.read_text(errors="replace")


def test_http2_half_closed_connection_error(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """A connection error on a half-closed session still writes the queued DATA.

    A connection error on a session that a graceful shutdown already
    half-closed sends no GOAWAY, so nothing flushes the DATA frames queued
    since. The FINI must flush them itself before it closes the session.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin_port = services.allocate_port()
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)
    _client = configure_client(services, _ats=_ats, _origin_port=_origin_port)

    _ats.start()
    verify(_client.run(timeout=30), _ats=_ats)
