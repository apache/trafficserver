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
"""Deferred HTTP/2 origin END_STREAM events must resume chunked tunnels."""

import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory, Curl


@pytest.mark.parametrize(
    "path,event", [("data-end-stream", "VC_EVENT_READ_COMPLETE"), ("trailers", "VC_EVENT_EOS"), ("empty-end-stream", None)]
)
def test_h2_origin_end_stream(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, path: str, event: str | None) -> None:
    """Deliver the terminating chunk before the inactivity timeout can mask a stall.

    :param ats_factory: Proxy factory.
    :param services: Custom origin factory.
    :param curl: HTTP/1.1 client.
    :param path: Origin framing case.
    :param event: Expected deferred event, or None for the immediate-close path.
    """
    directory = Path(__file__).parent
    ssl = directory.parents[1] / "tools/ssl"
    port = services.allocate_port()
    origin = services.process(
        "origin",
        [sys.executable, directory / "h2_end_stream_origin.py", str(port), ssl / "server.pem", ssl / "server.key"],
        ready_port=port,
    )
    origin.stdout.excludes("queue_body_failed", "Validate the origin's framing and request handling")
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_tunnel|http2_stream",
            "proxy.config.http.default_buffer_water_mark": 4096,
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.http.server_session_sharing.match": "none",
            "proxy.config.http.transaction_no_activity_timeout_out": 10,
        }
    )
    ats.remap_config.add_line(f"map / https://127.0.0.1:{port}/")
    origin.start()
    ats.start()
    result = curl.run_for(
        ats,
        f'-s --http1.1 --max-time 5 -o /dev/null -w "%{{size_download}} %{{http_code}}" http://127.0.0.1:{ats.http_port}/{path}',
    )
    assert result.returncode == 0, result.output
    assert result.stdout == "60000 200", result.output
    assert origin.is_running
    ats.stop()
    output = ats.traffic_out.read_text(errors="replace")
    assert "producer_handler [http server VC_EVENT_INACTIVITY_TIMEOUT" not in output
    if event is not None:
        assert f"defer {event} until the read VIO is re-enabled" in output
        assert f"rescheduled deferred {event}" in output
