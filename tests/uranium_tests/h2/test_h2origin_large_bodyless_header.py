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
"""Bodyless HTTP/2 responses may span many IOBuffer blocks."""

import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin


def test_h2origin_large_bodyless_header(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Deliver origin status and every field for headers up to 900 KiB.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    """
    port = services.allocate_port()
    script = Path(__file__).with_name("h2_large_header_origin.py")
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin",
        [sys.executable, script, "serve", "127.0.0.1", str(port), "--cert", ssl / "server.pem", "--key", ssl / "server.key"],
        ready_port=port,
    )
    ats = configure_h2_origin(
        ats_factory,
        port,
        **{
            "proxy.config.http.response_header_max_size": 1024 * 1024,
            "proxy.config.http.header_field_max_size": 65535,
            "proxy.config.http2.max_header_list_size": 1024 * 1024,
            "proxy.config.http2.max_continuation_frames_per_minute": 1000,
        },
    )
    origin.start()
    ats.start()
    cases = [
        (3800, 200, False),
        (4300, 200, False),
        (9 * 1024, 302, False),
        (9 * 1024, 204, False),
        (9 * 1024, 200, True),
        (16 * 1024, 302, False),
        (64 * 1024, 302, False),
        (128 * 1024, 204, False),
        (256 * 1024, 302, False),
        (512 * 1024, 204, False),
        (900 * 1024, 302, False),
        (512 * 1024, 200, True),
    ]
    for index, (size, status, body) in enumerate(cases):
        path = f"/{size}/{status}" + ("/body" if body else "")
        result = services.process(f"client-{index}", [sys.executable, script, "check", "127.0.0.1", str(ats.http_port), path]).run()
        assert result.returncode == 0, result.output
        assert "PASS" in result.stdout
    assert origin.is_running
    assert ats.is_running
