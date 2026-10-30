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
"""Solicited client SETTINGS ACKs must not consume the receive limit."""

import re
import sys
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory
from .origin_test_helpers import wait_for_metrics


def test_h2_settings_ack_inbound(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Acknowledge dynamic windows with three concurrent open streams.

    :param ats_factory: Proxy factory.
    :param services: Custom frame client factory.
    """
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http2.flow_control.policy_in": 2,
            "proxy.config.http2.max_settings_frames_per_minute": 2,
            "proxy.config.http.request_buffer_enabled": 1,
            "proxy.config.http2.initial_window_size_in": 65535,
            "proxy.config.http2.max_concurrent_streams_in": 100,
        }
    )
    ats.remap_config.add_line("map / http://127.0.0.1:1")
    ats.start()
    result = services.process(
        "client", [sys.executable, Path(__file__).with_name("settings_ack_client.py"), str(ats.https_port)]
    ).run(timeout=15)
    assert result.returncode == 0, result.output
    assert re.search(r"dynamic_settings_acknowledged=([3-9]|[1-9][0-9]+)", result.stdout), result.output
    wait_for_metrics(
        ats,
        "h2-settings-ack-metrics.gold",
        ["proxy.process.http2.max_settings_frames_per_minute_exceeded", "proxy.process.http2.connection_errors"],
    )
