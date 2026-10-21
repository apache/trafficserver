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
"""Locally half-closed origin sessions must not reenter the reusable pool."""
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin, finish_origin


def test_h2_origin_half_close(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Keep a draining origin alive across two subsequent requests.

    :param ats_factory: Proxy factory.
    :param services: Custom origin and client factory.
    """
    port = services.allocate_port()
    ats = configure_h2_origin(ats_factory, port, **{"proxy.config.http.transaction_no_activity_timeout_out": 2})
    script = Path(__file__).with_name("half_close_probe.py")
    stop = ats.run_directory / "stop"
    ssl = Path(__file__).parents[2] / "tools/ssl"
    origin = services.process(
        "origin", [sys.executable, script, "origin",
                   str(port), ssl / "server.pem", ssl / "server.key", stop], ready_port=port)
    origin.stdout.contains("half_close sessions=2 requests=3", "Validate the origin's framing and request handling")
    origin.start()
    ats.start()
    result = services.process(
        "client", [sys.executable, script, "client", str(ats.http_port), stop, ats.traffic_out]).run(timeout=30)
    assert result.returncode == 0, result.output
    assert origin.is_running
    finish_origin(services, origin, stop)
    ats.stop()
    assert "half_close state" not in ats.traffic_out.read_text(errors="replace")
