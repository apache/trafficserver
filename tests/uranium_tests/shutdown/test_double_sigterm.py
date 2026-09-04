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
"""A second exit signal must not schedule a second graceful shutdown."""

import signal
import time
from tools.uranium.services import ATS, wait_for_file_lines


def test_double_sigterm(ats: ATS) -> None:
    """Send SIGTERM on separate signal-continuation ticks during draining.

    :param ats: Proxy whose expected exit is explicitly awaited.
    """
    ats.records.update(
        {"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "server", "proxy.config.stop.shutdown_timeout": 3}
    )
    ats.start()
    ats.send_signal(signal.SIGTERM)
    wait_for_file_lines(ats.traffic_out, "received exit signal, shutting down in 3secs", 1)
    time.sleep(1)
    ats.send_signal(signal.SIGTERM)
    ats.wait(timeout=10)
    output = ats.traffic_out.read_text(errors="replace")
    assert output.count("received exit signal, shutting down in 3secs") == 1, output
    assert "received exit signal, shutdown already scheduled" in output
