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
"""Exercise async-handshake eventfd teardown on the 9.2 engine."""

from pathlib import Path
import re
import sys

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_file_lines
from uranium_tests.tls.test_tls_engine import configure_engine


def test_tls_engine_abort(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Survive 30 aborted async handshakes and then serve a normal request.

    :param ats_factory: Factory for the TLS proxy.
    :param services: Factory for the origin and abort client.
    :param curl: Transport-aware request runner.
    """
    ats, origin = configure_engine(ats_factory, services)
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "ssl",
        })
    origin.start()
    ats.start()
    aborted = ats.run(sys.executable, Path(__file__).with_name("tls_engine_abort.py"), str(ats.https_port), "30", timeout=60)
    assert aborted.returncode == 0, aborted.output
    assert "sent 30 aborted handshakes" in aborted.stdout, aborted.output
    # Require an async completion from the abort barrage itself, not merely
    # from the healthy request below.
    wait_for_file_lines(ats.traffic_out, "Send signal to ", 1)
    result = curl.run_for(
        ats, f"--insecure --verbose --header uuid:basic --header host:example.com https://127.0.0.1:{ats.https_port}/", timeout=30)
    assert result.returncode == 0, result.output
    assert re.search(r"HTTP/(2|1\.1) 200", result.stderr), result.output
    assert result.stdout == "ok", result.output
    output = wait_for_file_lines(ats.traffic_out, "Send signal to ", 1)
    assert "AddressSanitizer" not in output, output
