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
"""Split response and trailer fields on non-IDLE origin streams."""
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin, finish_origin


def test_h2_to_origin_continuation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Preserve fragmented headers, early responses, trailers, and empty DATA.

    :param ats_factory: Proxy factory.
    :param services: Custom frame origin and client factory.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    ats = configure_h2_origin(
        ats_factory, port, **{
            "proxy.config.http2.incomplete_header_timeout_in": 1,
            "proxy.config.http2.max_frame_size": 16384,
            "proxy.config.http2.max_header_list_size": 1048576,
            "proxy.config.http.response_header_max_size": 65536,
        })
    stop = ats.run_directory / "stop-origin"
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin",
        [sys.executable, directory / "continuation_origin.py",
         str(port), ssl / "server.pem", ssl / "server.key", "7", stop],
        ready_port=port)
    for expression in (r"sent_continuation_frames=[1-9][0-9]*", "sent_trailer_continuation=1", "304_empty_data_accepted=1"):
        origin.stdout.contains(expression, "Validate the origin's framing and request handling")
    origin.start()
    ats.start()
    replay = directory / "replay_h2o_continuation/continuation.replay.yaml"
    result = services.verifier_client("client", replay, http_ports=[ats.http_port], https_ports=[ats.https_port]).run(timeout=60)
    assert result.returncode == 0, result.output
    result = services.process("verify-headers", [sys.executable, directory / "verify_continuation_headers.py",
                                                 str(ats.http_port)]).run()
    assert result.returncode == 0, result.output
    for mode in ("early", "trailers", "slow", "304-data"):
        result = services.process(
            mode, [sys.executable, directory / "continuation_probe.py",
                   str(ats.http_port),
                   str(ats.https_port), mode, stop]).run(timeout=20)
        assert result.returncode == 0, result.output
        assert origin.is_running
        assert ats.is_running
    finish_origin(services, origin, stop)
    ats.stop()
    assert "continuation bad state" not in ats.diags_log.read_text(errors="replace")
    assert "_sm == nullptr" not in ats.traffic_out.read_text(errors="replace")
