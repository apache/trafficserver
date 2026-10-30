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
"""Independent stream and connection receive windows for all policies."""

import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize("policy", (0, 1, 2))
@pytest.mark.parametrize("window", (65535, 32768))
def test_h2_initial_receive_window(ats_factory: ATSFactory, services: ServiceFactory, policy: int, window: int) -> None:
    """Check preface and subsequent stream windows.

    :param ats_factory: Proxy factory.
    :param services: Custom client factory.
    :param policy: HTTP/2 receive-window policy.
    :param window: Initial stream window in bytes.
    """
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.http.server_ports": f"{ats.http_port}:proto=http2",
            "proxy.config.http2.flow_control.policy_in": policy,
            "proxy.config.http2.initial_window_size_in": window,
            "proxy.config.http2.max_concurrent_streams_in": 100,
            "proxy.config.http.request_buffer_enabled": 1,
        }
    )
    ats.remap_config.add_line("map / http://127.0.0.1:1")
    ats.start()
    result = services.process(
        "client",
        [sys.executable, Path(__file__).with_name("initial_window_client.py"), str(ats.http_port), str(policy), str(window)],
    ).run(timeout=15)
    assert result.returncode == 0, result.output
    assert f"verified policy={policy} stream_window={window}" in result.stdout
