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
"""Exercise nested half-close while dechunking a backpressured response."""
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize("method", ("POST", "GET"))
def test_dechunk_post_half_close(ats_factory: ATSFactory, services: ServiceFactory, method: str) -> None:
    """Drain a delayed final chunk without leaving a cleared write VIO active.

    :param ats_factory: Proxy factory.
    :param services: Custom origin factory.
    :param method: POST exercises half-close; GET is the full-close control.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process(
        "origin",
        [sys.executable, directory / "dechunk_post_half_close_origin.py",
         str(port), "--body-size", "24000", "--hold", "0.5"],
        ready_port=port)
    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_tunnel|http_cs",
            "proxy.config.http.chunking_enabled": 0,
            "proxy.config.http.default_buffer_water_mark": 1,
            "proxy.config.http.default_buffer_size": 8,
            "proxy.config.net.sock_send_buffer_size_in": 4096,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/")
    origin.start()
    ats.start()
    result = ats.run(
        sys.executable,
        directory / "dechunk_post_half_close_client.py",
        str(ats.http_port),
        "--method",
        method,
        "--stall",
        "1.0",
        "--sip",
        "512",
        "--interval",
        "0.02",
        timeout=90)
    assert result.returncode == 0, result.output
    assert "status: HTTP/1.1 200" in result.stdout, result.output
    assert "body bytes: 24000" in result.stdout, result.output
    ats.stop()
    assert ("session half close" in ats.traffic_out.read_text()) == (method == "POST")
