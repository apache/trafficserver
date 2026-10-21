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
"""HTTP/2 receive-window policies change without restarting ATS."""
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import configure_h2_origin


def test_http2_flow_control_reload(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Observe inbound and outbound connection windows across live reloads.

    :param ats_factory: Proxy factory.
    :param services: Probe process and port factory.
    """
    port = services.allocate_port()
    ats = configure_h2_origin(
        ats_factory, port, **{
            "proxy.config.http2.initial_window_size_in": 65535,
            "proxy.config.http2.initial_window_size_out": 65535,
            "proxy.config.http2.max_concurrent_streams_in": 100,
            "proxy.config.http2.max_concurrent_streams_out": 100,
            "proxy.config.http2.flow_control.policy_in": 0,
            "proxy.config.http2.flow_control.policy_out": 0,
        })
    ats.start()
    result = services.process(
        "probe", [
            sys.executable,
            Path(__file__).with_name("http2_flow_control_reload.py"), "--https-port",
            str(ats.https_port), "--http-port",
            str(ats.http_port), "--origin-port",
            str(port), "--cert", ats.ssl_directory / "server.pem", "--key", ats.ssl_directory / "server.key"
        ],
        environment=ats.environment).run(timeout=180)
    assert result.returncode == 0, result.output
    assert "PASS: both flow control policies reloaded" in result.stdout
